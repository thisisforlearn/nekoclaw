#include "nekoclaw/bitboard.h"
#include "nekoclaw/magic.h"

namespace nekoclaw {

Bitboard kPawnAttacks[2][64];
Bitboard kKnightAttacks[64];
Bitboard kKingAttacks[64];
Bitboard kBetween[64][64];
Bitboard kLine[64][64];

static Bitboard compute_pawn_attacks(Color c, Square s){
  int f = file_of(s), r = rank_of(s);
  Bitboard b = 0;
  if(c == WHITE){
    if(r < 7 && f > 0) b |= square_bb(make_square(f-1, r+1));
    if(r < 7 && f < 7) b |= square_bb(make_square(f+1, r+1));
  } else {
    if(r > 0 && f > 0) b |= square_bb(make_square(f-1, r-1));
    if(r > 0 && f < 7) b |= square_bb(make_square(f+1, r-1));
  }
  return b;
}

void init_bitboards(){
  for(Square s = 0; s < 64; ++s){
    kPawnAttacks[WHITE][s] = compute_pawn_attacks(WHITE, s);
    kPawnAttacks[BLACK][s] = compute_pawn_attacks(BLACK, s);
  }

  for(Square s = 0; s < 64; ++s){
    Bitboard b = 0;
    int f = file_of(s), r = rank_of(s);
    static const int nd[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
    for(auto& d : nd){
      int nf = f+d[0], nr = r+d[1];
      if(nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
        b |= square_bb(make_square(nf, nr));
    }
    kKnightAttacks[s] = b;
  }

  for(Square s = 0; s < 64; ++s){
    Bitboard b = 0;
    int f = file_of(s), r = rank_of(s);
    for(int df = -1; df <= 1; ++df)
      for(int dr = -1; dr <= 1; ++dr){
        if(df == 0 && dr == 0) continue;
        int nf = f+df, nr = r+dr;
        if(nf >= 0 && nf < 8 && nr >= 0 && nr < 8)
          b |= square_bb(make_square(nf, nr));
      }
    kKingAttacks[s] = b;
  }

  for(Square a = 0; a < 64; ++a)
    for(Square b = 0; b < 64; ++b){
      kBetween[a][b] = 0;
      kLine[a][b] = 0;
      if(a == b) continue;

      int fa = file_of(a), ra = rank_of(a);
      int fb = file_of(b), rb = rank_of(b);
      int df = (fb > fa) - (fb < fa);
      int dr = (rb > ra) - (rb < ra);

      bool aligned = (fa == fb) || (ra == rb) ||
                     (std::abs(fb - fa) == std::abs(rb - ra));
      if(!aligned) continue;

      // Between: squares strictly between a and b
      Bitboard bet = 0;
      int cf = fa + df, cr = ra + dr;
      while(cf != fb || cr != rb){
        bet |= square_bb(make_square(cf, cr));
        cf += df; cr += dr;
      }
      kBetween[a][b] = bet;

      // Full line through a and b
      Bitboard line = square_bb(a) | square_bb(b);
      cf = fa - df; cr = ra - dr;
      while(cf >= 0 && cf < 8 && cr >= 0 && cr < 8){
        line |= square_bb(make_square(cf, cr)); cf -= df; cr -= dr;
      }
      cf = fb + df; cr = rb + dr;
      while(cf >= 0 && cf < 8 && cr >= 0 && cr < 8){
        line |= square_bb(make_square(cf, cr)); cf += df; cr += dr;
      }
      kLine[a][b] = line;
    }

  init_magics();
}

} // namespace nekoclaw
