#include "nekoclaw/movegen.h"
#include "nekoclaw/bitboard.h"
#include "nekoclaw/magic.h"
#include <cassert>

namespace nekoclaw {

static inline void add_move(ExtMove* &list, Move m, int score=0){
  list->move = m;
  list->value = score;
  ++list;
}

// MVV-LVA for captures
static const int kMvvLva[6][6] = {
// victim: P N B R Q K (K not captured normally)
  {105,205,305,405,505,605},
  {104,204,304,404,504,604},
  {103,203,303,403,503,603},
  {102,202,302,402,502,602},
  {101,201,301,401,501,601},
  {100,200,300,400,500,600}
};

static int mvv_lva(PieceType attacker, Piece victim){
  if(victim==NO_PIECE) return 0;
  PieceType vt = type_of(victim);
  return kMvvLva[int(vt)][int(attacker)] + 10000; // captures sorted high
}

// Generate pawn moves
static ExtMove* gen_pawns(const Position& pos, ExtMove* list, Bitboard target){
  Color us = pos.side_to_move();
  Color them = opposite(us);
  Bitboard pawns = pos.pieces(us, PAWN);
  Bitboard occ = pos.occupancy();
  Bitboard enemies = pos.pieces(them);
  Bitboard empty = ~occ;

  int push = (us==WHITE?8:-8);
  int startRank = (us==WHITE?1:6);
  int promoRank = (us==WHITE?6:1);

  Bitboard cur = pawns;
  while(cur){
    Square from = lsb(cur); cur &= cur-1;
    int r = rank_of(from);
    // single and double pawn pushes
    Square to1 = Square(from+push);
    if(to1>=0 && to1<64 && (empty & square_bb(to1))){
      bool isPromo = (r==promoRank);
      if(target & square_bb(to1)){
        if(isPromo){
          for(PieceType pt: {QUEEN, ROOK, BISHOP, KNIGHT}){
            Move m = make_promotion(from,to1,pt);
            int score = isPromo? 800000 + int(pt) : 0;
            add_move(list, m, score);
          }
        } else {
          add_move(list, make_move(from,to1), 0);
        }
      }
      // double push (can block check even if single square not in target)
      if(r==startRank){
        Square to2 = Square(from+push*2);
        if(to2>=0 && to2<64 && (empty & square_bb(to2)) && (target & square_bb(to2))){
          add_move(list, make_move(from,to2, MoveFlag::DOUBLE_PUSH), 0);
        }
      }
    }
    // captures (including en passant only if pawn attacks ep square)
    Bitboard caps = pawn_attacks(us, from) & enemies;
    // en passant: only if this pawn attacks ep square
    if(pos.state()->epSquare!=SQ_NONE) {
      Bitboard epBB = square_bb(pos.state()->epSquare);
      if(pawn_attacks(us, from) & epBB) caps |= epBB;
    }
    caps &= target;
    while(caps){
      Square to = lsb(caps); caps &= caps-1;
      bool isPromo = (r==promoRank);
      Piece cap = pos.piece_on(to);
      bool isEp = (to==pos.state()->epSquare && cap==NO_PIECE);
      uint32_t flags = 0;
      if(isEp) flags |= MoveFlag::ENPASSANT;
      if(cap!=NO_PIECE || isEp) flags |= MoveFlag::CAPTURE;
      if(isPromo){
        for(PieceType pt: {QUEEN, ROOK, BISHOP, KNIGHT}){
          Move m = make_promotion(from,to,pt);
          if(isEp) m |= MoveFlag::ENPASSANT;
          if(cap!=NO_PIECE) m |= MoveFlag::CAPTURE;
          int score = mvv_lva(PAWN, cap==NO_PIECE? B_PAWN: cap) + 500;
          add_move(list, m, score);
        }
      } else {
        Move m = make_move(from,to, flags);
        int score = mvv_lva(PAWN, pos.piece_on(to==pos.state()->epSquare? (us==WHITE?to-8:to+8):to));
        add_move(list, m, score);
      }
    }
  }
  return list;
}

static ExtMove* gen_knights(const Position& pos, ExtMove* list, Bitboard target){
  Color us = pos.side_to_move();
  Bitboard knights = pos.pieces(us, KNIGHT);
  while(knights){
    Square from = lsb(knights); knights&=knights-1;
    Bitboard att = knight_attacks(from) & target;
    // remove own pieces already via target, but target for evasions may include checks
    while(att){
      Square to = lsb(att); att&=att-1;
      Piece cap = pos.piece_on(to);
      uint32_t flags = cap!=NO_PIECE? MoveFlag::CAPTURE:0;
      int score = cap!=NO_PIECE? mvv_lva(KNIGHT, cap):0;
      add_move(list, make_move(from,to,flags), score);
    }
  }
  return list;
}

static ExtMove* gen_bishops(const Position& pos, ExtMove* list, Bitboard target){
  Color us = pos.side_to_move();
  Bitboard bishops = pos.pieces(us,BISHOP);
  Bitboard occ = pos.occupancy();
  while(bishops){
    Square from = lsb(bishops); bishops&=bishops-1;
    Bitboard att = bishop_attacks(from, occ) & target;
    while(att){
      Square to = lsb(att); att&=att-1;
      Piece cap = pos.piece_on(to);
      uint32_t flags = cap!=NO_PIECE? MoveFlag::CAPTURE:0;
      int score = cap!=NO_PIECE? mvv_lva(BISHOP, cap):0;
      add_move(list, make_move(from,to,flags), score);
    }
  }
  return list;
}

static ExtMove* gen_rooks(const Position& pos, ExtMove* list, Bitboard target){
  Color us = pos.side_to_move();
  Bitboard rooks = pos.pieces(us,ROOK);
  Bitboard occ = pos.occupancy();
  while(rooks){
    Square from = lsb(rooks); rooks&=rooks-1;
    Bitboard att = rook_attacks(from, occ) & target;
    while(att){
      Square to = lsb(att); att&=att-1;
      Piece cap = pos.piece_on(to);
      uint32_t flags = cap!=NO_PIECE? MoveFlag::CAPTURE:0;
      int score = cap!=NO_PIECE? mvv_lva(ROOK, cap):0;
      add_move(list, make_move(from,to,flags), score);
    }
  }
  return list;
}

static ExtMove* gen_queens(const Position& pos, ExtMove* list, Bitboard target){
  Color us = pos.side_to_move();
  Bitboard queens = pos.pieces(us,QUEEN);
  Bitboard occ = pos.occupancy();
  while(queens){
    Square from = lsb(queens); queens&=queens-1;
    Bitboard att = queen_attacks(from, occ) & target;
    while(att){
      Square to = lsb(att); att&=att-1;
      Piece cap = pos.piece_on(to);
      uint32_t flags = cap!=NO_PIECE? MoveFlag::CAPTURE:0;
      int score = cap!=NO_PIECE? mvv_lva(QUEEN, cap):0;
      add_move(list, make_move(from,to,flags), score);
    }
  }
  return list;
}

static ExtMove* gen_king(const Position& pos, ExtMove* list, Bitboard target){
  Color us = pos.side_to_move();
  Square ksq = pos.king_square(us);
  Bitboard att = king_attacks(ksq) & target;
  while(att){
    Square to = lsb(att); att&=att-1;
    Piece cap = pos.piece_on(to);
    // king cannot move into check — legality filter later, but we generate pseudo-legal
    uint32_t flags = cap!=NO_PIECE? MoveFlag::CAPTURE:0;
    int score = cap!=NO_PIECE? mvv_lva(KING, cap):0;
    add_move(list, make_move(ksq,to,flags), score);
  }
  // castling
  if(pos.in_check()) return list;
  int cr = pos.state()->castling;
  Bitboard occ = pos.occupancy();
  Color them = opposite(us);
  auto canCastle = [&](Square kingFrom, Square kingTo, Square rookFrom, Square rookTo, int flag, Bitboard between){
    if(!(cr & flag)) return false;
    if(occ & between) return false;
    // king path must not be attacked
    Square step = (kingTo > kingFrom? Square(kingFrom+1): Square(kingFrom-1));
    if(pos.is_attacked(kingFrom, them)) return false;
    if(pos.is_attacked(step, them)) return false;
    if(pos.is_attacked(kingTo, them)) return false;
    if(pos.piece_on(rookFrom)!=make_piece(us,ROOK)) return false;
    return true;
  };
  if(us==WHITE){
    if(canCastle(E1,G1,H1,F1,1, square_bb(F1)|square_bb(G1))) add_move(list, make_move(E1,G1,MoveFlag::CASTLING), 0);
    if(canCastle(E1,C1,A1,D1,2, square_bb(B1)|square_bb(C1)|square_bb(D1))) add_move(list, make_move(E1,C1,MoveFlag::CASTLING), 0);
  } else {
    if(canCastle(E8,G8,H8,F8,4, square_bb(F8)|square_bb(G8))) add_move(list, make_move(E8,G8,MoveFlag::CASTLING), 0);
    if(canCastle(E8,C8,A8,D8,8, square_bb(B8)|square_bb(C8)|square_bb(D8))) add_move(list, make_move(E8,C8,MoveFlag::CASTLING), 0);
  }
  return list;
}

int generate(const Position& pos, ExtMove* list, GenType type){
  ExtMove* start = list;
  Bitboard target = ~0ULL;

  if(type==EVASIONS){
    // only moves that get out of check
    Bitboard checkers = pos.checkers();
    Square ksq = pos.king_square(pos.side_to_move());
    // king moves always
    list = gen_king(pos, list, ~pos.pieces(pos.side_to_move()));
    if(popcount(checkers)>1) return int(list-start); // only king moves in double check
    Square checker = lsb(checkers);
    Bitboard between = kBetween[ksq][checker] | square_bb(checker);
    // block or capture checker with other pieces
    list = gen_pawns(pos, list, between);
    list = gen_knights(pos, list, between);
    list = gen_bishops(pos, list, between);
    list = gen_rooks(pos, list, between);
    list = gen_queens(pos, list, between);
    // king already done
    return int(list-start);
  }

  if(type==CAPTURES){
    target = pos.pieces(opposite(pos.side_to_move()));
    // also include en passant
    if(pos.state()->epSquare!=SQ_NONE) target |= square_bb(pos.state()->epSquare);
    // pawn promotions are captures if they capture
  } else if(type==QUIETS){
    target = ~pos.occupancy();
    // but we will filter captures out by checking flags
  } else {
    target = ~pos.pieces(pos.side_to_move());
  }

  // order: pawns, knights, bishops, rooks, queens, king
  list = gen_pawns(pos, list, target);
  list = gen_knights(pos, list, target);
  list = gen_bishops(pos, list, target);
  list = gen_rooks(pos, list, target);
  list = gen_queens(pos, list, target);
  list = gen_king(pos, list, target);

  if(type==QUIETS){
    // remove captures from list (those with CAPTURE flag or promo capture)
    ExtMove* write = start;
    for(ExtMove* read=start; read<list; ++read){
      if(!(read->move & MoveFlag::CAPTURE)) { *write++ = *read; }
    }
    list = write;
  } else if(type==CAPTURES){
    ExtMove* write = start;
    for(ExtMove* read=start; read<list; ++read){
      if(read->move & MoveFlag::CAPTURE) { *write++ = *read; }
    }
    list = write;
  }

  return int(list-start);
}

int generate_legal(const Position& pos, ExtMove* list){
  ExtMove pseudo[MAX_MOVES];
  int cnt = generate(pos, pseudo, pos.in_check()? EVASIONS : LEGAL);
  // LEGAL fallback: generate all and filter via is_legal
  // For EVASIONS we already filtered double-check correctly, but need legality for pins
  int n=0;
  for(int i=0;i<cnt;++i){
    Move m = pseudo[i].move;
    if(pos.is_legal(m)) list[n++]=pseudo[i];
  }
  if(cnt==0 && !pos.in_check()){
    // LEGAL was passed, we handled via pseudo LEGAL? Actually generate with LEGAL not implemented for non-evasions
    // So if type LEGAL and not in check, generate all pseudo and filter
  }
  return n;
}

Value see(const Position& pos, Move m){
  // Simplified SEE: MVV-LVA with sliding recaptures
  // This is not full SEE swap algorithm but gives reasonable ordering
  // For true SEE, we need to simulate captures on the to square
  Square from = move_from(m), to = move_to(m);
  Piece attacker = pos.piece_on(from);
  Piece victim = pos.piece_on(to);
  if(m & MoveFlag::ENPASSANT) victim = make_piece(opposite(pos.side_to_move()), PAWN);
  if(victim==NO_PIECE) return 0;
  static const int vals[6]={100,320,330,500,900,20000};
  int gain = vals[type_of(victim)] - vals[type_of(attacker)]/10;
  // If attacker is slinding and there are xrays, recurse would be needed
  // For ordering we just return MVV-LVA
  return Value(gain);
}

bool see_ge(const Position& pos, Move m, Value threshold){
  return see(pos,m) >= threshold;
}

} // namespace nekoclaw
