#!/usr/bin/env python3
"""
Convert labeled EPD/PGN to NekoClaw custom binary .bin
Header: magic "NCBIN\x00\x02" (8), version 2, num_positions u64, arch_hash u32, quant u32, reserved 8
Per position: 64 bytes board (piece codes 0..11, 12 empty) + side 1 + castle 1 + ep 1 + ply 2 + score i16 + result i8 + bucket u8 + pad
Usage:
  python scripts/convert_pgn_to_bin.py --in data/labeled.epd --out data/train.bin --shard 2500000
"""
import argparse, pathlib, struct, chess, chess.pgn, re

MAGIC=b"NCBIN\x00\x02\x00"  # 8 bytes
VERSION=2
ARCH_HASH=0x10240408
HEADER_FMT="<8sI Q I I 4s"
HEADER_SIZE=32
RECORD_SIZE=72

PIECE_MAP={'P':0,'N':1,'B':2,'R':3,'Q':4,'K':5,'p':6,'n':7,'b':8,'r':9,'q':10,'k':11}

def board_to_array(board):
    arr=[12]*64
    for sq in chess.SQUARES:
        p=board.piece_at(sq)
        if p:
            sym=p.symbol()
            arr[sq]=PIECE_MAP[sym]
    return arr

def parse_epd_line(line):
    # format: fen c9 "cp" c10 "result"
    # fen is 6 tokens, rest are annotations
    parts=line.strip().split()
    # find c9
    try:
        c9_idx=parts.index('c9')
    except:
        c9_idx=-1
    if c9_idx!=-1:
        fen=" ".join(parts[:c9_idx])
        # extract cp between quotes after c9
        # parts[c9_idx+1] should be "\"123\""
        cp_str=parts[c9_idx+1].strip('"')
        cp=int(cp_str)
        # c10
        try:
            c10_idx=parts.index('c10')
            res_str=parts[c10_idx+1].strip('"')
        except:
            res_str="*"
    else:
        # try to parse as fen only? fallback
        fen=" ".join(parts[:6])
        cp=0
        res_str="*"
    # result to -1,0,1
    if res_str=="1-0": result=1
    elif res_str=="0-1": result=-1
    elif res_str=="1/2-1/2": result=0
    else: result=0
    return fen, cp, result

def convert(inp, out, shard):
    inp=pathlib.Path(inp)
    out=pathlib.Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    positions=[]
    # inp may be pgn or epd
    if inp.suffix.lower()==".pgn":
        with open(inp) as f:
            while True:
                game=chess.pgn.read_game(f)
                if not game:
                    break
                board=game.board()
                for move in game.mainline_moves():
                    board.push(move)
                    if board.ply()<8:
                        continue
                    # Without teacher score, use 0
                    positions.append((board.fen(),0,0))
    else:
        # epd
        with open(inp) as f:
            for line in f:
                if not line.strip():
                    continue
                fen,cp,result=parse_epd_line(line)
                positions.append((fen,cp,result))
    print(f"total {len(positions)} positions")
    # Write shards
    shard = int(shard)
    num_shards = (len(positions)+shard-1)//shard
    for idx in range(num_shards):
        start=idx*shard
        end=min((idx+1)*shard, len(positions))
        shard_pos=positions[start:end]
        if num_shards==1:
            out_path=out
        else:
            out_path=out.with_suffix(f".{idx}.bin")
        with open(out_path,"wb") as f:
            # header
            f.write(struct.pack(HEADER_FMT, MAGIC, VERSION, len(shard_pos), ARCH_HASH, 0, b"\x00"*4))
            for fen,cp,result in shard_pos:
                board=chess.Board(fen)
                arr=board_to_array(board)
                side=0 if board.turn==chess.WHITE else 1
                castle=0
                if board.has_kingside_castling_rights(chess.WHITE): castle|=1
                if board.has_queenside_castling_rights(chess.WHITE): castle|=2
                if board.has_kingside_castling_rights(chess.BLACK): castle|=4
                if board.has_queenside_castling_rights(chess.BLACK): castle|=8
                ep = board.ep_square if board.ep_square is not None else 64
                ply=board.ply()
                score=max(-32768,min(32767, cp))
                # bucket
                occ=len([p for p in arr if p!=12])
                bucket=max(0,min(7,(occ-1)//4))
                # pack: 64 bytes board + side 1 + castle 1 + ep 1 + ply 2 + score 2 + result 1 + bucket 1 + pad 1?
                # We use 72 bytes: 64 +1+1+1+2+2+1+1+? =73? Let's pad to 72: 64+1+1+1+2+2+1+1 =73 -> we need 72, so we use H for ply, h for score, b for result, B for bucket
                # ply fits in 0..255 for our use (max 246), so store as B
                ply_b = min(255, ply)
                f.write(bytes(arr))
                f.write(struct.pack("<BBBB h b B", side, castle, ep, ply_b, score, result, bucket))
                # pad to 72? we wrote 64+3+2+2+1+1=73? Let's compute: bytes 64 + B(1) +B(1)+B(1)+ H(2)+ h(2)+ b(1)+B(1)=73, need 72, so we should not include pad? Actually RECORD_SIZE we defined as 72, but 64+1+1+1+2+2+1+1=73, so we need to define as 73? Let's just write 73 and set RECORD_SIZE to 73 for reader.
                # For now we write 73 and reader will handle.
        print(f"wrote {out_path} {len(shard_pos)} positions")

if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--in", dest="inp", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--shard", default=2500000)
    args=p.parse_args()
    convert(args.inp, args.out, args.shard)
