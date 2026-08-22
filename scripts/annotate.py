#!/usr/bin/env python3
"""
Annotate PGN with heavy teacher analysis (Stockfish depth>=22 or Lc0 nodes)
Usage:
  python scripts/annotate.py --in data/raw/gm.pgn --out data/labeled.epd --engine stockfish --engine-path /usr/games/stockfish --depth 22 --threads 8 --hash 2048 --filter-eco
This is the important step for training data quality.
"""
import argparse, pathlib, chess, chess.pgn, chess.engine, sys, re

def annotate(input_pgn, output_epd, engine_path, engine_type, depth, threads, hash_mb, multipv, filter_eco):
    out = pathlib.Path(output_epd)
    out.parent.mkdir(parents=True, exist_ok=True)
    # Stockfish
    if engine_type == "stockfish":
        engine = chess.engine.SimpleEngine.popen_uci(engine_path)
        engine.configure({"Threads": threads, "Hash": hash_mb})
    else:
        # lc0
        engine = chess.engine.SimpleEngine.popen_uci(engine_path)
        engine.configure({"Threads": threads})
    count=0
    with open(input_pgn) as pgn_in, open(out, "w") as epd_out:
        while True:
            game = chess.pgn.read_game(pgn_in)
            if not game:
                break
            board = game.board()
            for move in game.mainline_moves():
                board.push(move)
                # Filter: only positions where side to move not in check? Keep all
                # For training we want diverse positions: skip early opening book? We'll keep after ply 8
                if board.ply() < 8:
                    continue
                # Analyze
                info = engine.analyse(board, chess.engine.Limit(depth=depth))
                score = info["score"].white()
                # Convert to centipawns from white perspective
                try:
                    cp = score.score()
                    if cp is None:
                        # mate score
                        mate = score.mate()
                        cp = 20000 if mate>0 else -20000
                except:
                    cp = 0
                # Clamp
                cp = max(-15000, min(15000, cp))
                # Result from game headers
                result = game.headers.get("Result", "*")
                # Write EPD with annotations: fen + cp + result
                fen = board.fen()
                # For custom bin, we will store fen, cp, result, bucket
                epd_out.write(f"{fen} c9 \"{cp}\" c10 \"{result}\"\n")
                count+=1
                if count % 1000 == 0:
                    print(f"annotated {count} positions")
    engine.quit()
    print(f"done {count} positions to {out}")

if __name__ == "__main__":
    p=argparse.ArgumentParser()
    p.add_argument("--in", dest="inp", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--engine", choices=["stockfish","lc0"], default="stockfish")
    p.add_argument("--engine-path", default="/usr/games/stockfish")
    p.add_argument("--depth", type=int, default=22, help="stockfish depth, must be >=22 for high quality")
    p.add_argument("--threads", type=int, default=8)
    p.add_argument("--hash", type=int, default=2048)
    p.add_argument("--multipv", type=int, default=1)
    p.add_argument("--filter-eco", action="store_true")
    args=p.parse_args()
    annotate(args.inp, args.out, args.engine_path, args.engine, args.depth, args.threads, args.hash, args.multipv, args.filter_eco)
