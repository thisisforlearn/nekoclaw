#include "nekoclaw/nnue/features.h"
#include "nekoclaw/position.h"

namespace nekoclaw::nnue {

void append_features(const Position& pos, Color perspective, int* out, int& cnt){
  cnt=0;
  Square kingSq = pos.king_square(perspective);
  // Iterate all pieces
  for(Square s=0; s<64; ++s){
    Piece pc = pos.piece_on(s);
    if(pc==NO_PIECE) continue;
    // In HalfKAv2_hm, we don't encode our own king as piece? But we include all for simplicity (king piece also encoded, but alternative would skip)
    // Keep all 12 types.
    int idx = feature_index(perspective, kingSq, pc, s);
    out[cnt++] = idx;
  }
}

void make_dirty(const Position& before, const Position& after, DirtyState& ds, Color perspective){
  ds.numDirty=0;
  // Diff boards
  for(Square s=0; s<64; ++s){
    Piece a = before.piece_on(s);
    Piece b = after.piece_on(s);
    if(a==b) continue;
    if(a!=NO_PIECE && b!=NO_PIECE){
      // moved? captured? For simplicity treat as remove+add
      if(ds.numDirty<7) ds.dirty[ds.numDirty++] = {a, s, SQ_NONE};
      if(ds.numDirty<7) ds.dirty[ds.numDirty++] = {b, SQ_NONE, s};
    } else if(a!=NO_PIECE){
      if(ds.numDirty<7) ds.dirty[ds.numDirty++] = {a, s, SQ_NONE};
    } else {
      if(ds.numDirty<7) ds.dirty[ds.numDirty++] = {b, SQ_NONE, s};
    }
  }
  // If king moved, we need to handle bucket change — caller will detect and do full refresh instead
}

} // namespace
