#include "nekoclaw/magic.h"
#include "nekoclaw/bitboard.h"
#include <vector>

namespace nekoclaw {

Bitboard kRookTable[102400];
Bitboard kBishopTable[5248];
PextEntry kRookPext[64];
PextEntry kBishopPext[64];
Magic kRookMagics[64];
Magic kBishopMagics[64];

static Bitboard sliding_attack_pext(int sq, Bitboard occ, bool isRook) {
  Bitboard att = 0;
  int f = file_of(sq), r = rank_of(sq);
  static const int dirs_r[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
  static const int dirs_b[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
  const int (*dirs)[2] = isRook ? dirs_r : dirs_b;
  for (int d = 0; d < 4; ++d) {
    int cf = f + dirs[d][0], cr = r + dirs[d][1];
    while (cf >= 0 && cf < 8 && cr >= 0 && cr < 8) {
      Square to = make_square(cf, cr);
      att |= square_bb(to);
      if (occ & square_bb(to)) break;
      cf += dirs[d][0]; cr += dirs[d][1];
    }
  }
  return att;
}

static Bitboard rookMask(Square s) {
  Bitboard m = 0;
  int f = file_of(s), r = rank_of(s);
  for (int rr = r + 1; rr <= 6; ++rr) m |= square_bb(make_square(f, rr));
  for (int rr = r - 1; rr >= 1; --rr) m |= square_bb(make_square(f, rr));
  for (int ff = f + 1; ff <= 6; ++ff) m |= square_bb(make_square(ff, r));
  for (int ff = f - 1; ff >= 1; --ff) m |= square_bb(make_square(ff, r));
  return m;
}

static Bitboard bishopMask(Square s) {
  Bitboard m = 0;
  int f = file_of(s), r = rank_of(s);
  static const int d[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
  for (int i = 0; i < 4; ++i) {
    int cf = f + d[i][0], cr = r + d[i][1];
    while (cf >= 1 && cf <= 6 && cr >= 1 && cr <= 6) {
      m |= square_bb(make_square(cf, cr));
      cf += d[i][0]; cr += d[i][1];
    }
  }
  return m;
}

static inline Bitboard scatter(int idx, Bitboard mask) {
  Bitboard occ = 0;
  Bitboard m = mask;
  int b = 0;
  while (m) {
    Square s = lsb(m);
    m &= m - 1;
    if (idx & (1 << b)) occ |= square_bb(s);
    ++b;
  }
  return occ;
}

void init_magics() {
  // Build PEXT tables — O( sum 2^bits ) ~ 107k entries, ~0.8ms
  int rookOff = 0;
  for (int sq = 0; sq < 64; ++sq) {
    Bitboard mask = rookMask(Square(sq));
    int bits = popcount(mask);
    int size = 1 << bits;
    kRookPext[sq].mask = mask;
    kRookPext[sq].bits = bits;
    kRookPext[sq].offset = rookOff;
    kRookPext[sq].table = kRookTable + rookOff;
    // Legacy Magic for compat
    kRookMagics[sq].mask = mask;
    kRookMagics[sq].table = kRookTable + rookOff;
    kRookMagics[sq].shift = 64 - bits;
    kRookMagics[sq].magic = 0;

    for (int i = 0; i < size; ++i) {
      Bitboard occ = scatter(i, mask);
      Bitboard att = sliding_attack_pext(sq, occ, true);
      // PEXT index of occ is exactly i by construction
      kRookTable[rookOff + i] = att;
    }
    rookOff += size;
  }

  int bishopOff = 0;
  for (int sq = 0; sq < 64; ++sq) {
    Bitboard mask = bishopMask(Square(sq));
    int bits = popcount(mask);
    int size = 1 << bits;
    kBishopPext[sq].mask = mask;
    kBishopPext[sq].bits = bits;
    kBishopPext[sq].offset = bishopOff;
    kBishopPext[sq].table = kBishopTable + bishopOff;
    kBishopMagics[sq].mask = mask;
    kBishopMagics[sq].table = kBishopTable + bishopOff;
    kBishopMagics[sq].shift = 64 - bits;
    kBishopMagics[sq].magic = 0;

    for (int i = 0; i < size; ++i) {
      Bitboard occ = scatter(i, mask);
      Bitboard att = sliding_attack_pext(sq, occ, false);
      kBishopTable[bishopOff + i] = att;
    }
    bishopOff += size;
  }
}

Bitboard rook_attacks(Square sq, Bitboard occ) {
  const PextEntry& e = kRookPext[sq];
  return e.table[_pext_u64(occ, e.mask)];
}
Bitboard bishop_attacks(Square sq, Bitboard occ) {
  const PextEntry& e = kBishopPext[sq];
  return e.table[_pext_u64(occ, e.mask)];
}
// rook/bishop_attacks_fn already inlined in magic.h

} // namespace nekoclaw
