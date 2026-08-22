#include "nekoclaw/search/movepick.h"
#include "nekoclaw/movegen.h"
#include <algorithm>

namespace nekoclaw::search {

MovePicker::MovePicker(const Position& pos, Move ttMove, Depth depth, const History& hist, const Killers& killers, Move counter, bool qsearch)
: pos_(pos), hist_(&hist), ttMove_(ttMove), depth_(depth), qsearch_(qsearch), killers_(killers), counter_(counter) {
  stage_ = STAGE_TT;
  count_=0; idx_=0;
  // we will generate on demand
}

static int score_move(const Position& pos, Move m, const History& h, const Killers& killers, Move counter){
  if(pos.piece_on(move_to(m))!=NO_PIECE || (m & MoveFlag::ENPASSANT)){
    // capture: MVV-LVA + capture history
    PieceType pt = type_of(pos.piece_on(move_from(m)));
    Piece cap = pos.piece_on(move_to(m));
    if(m & MoveFlag::ENPASSANT) cap = make_piece(opposite(pos.side_to_move()), PAWN);
    int hist = h.get_capture(pt, cap, move_to(m));
    int mvv = 0;
    if(cap!=NO_PIECE){ static const int vals[6]={100,300,330,500,900,0}; mvv = vals[type_of(cap)]*10 - vals[pt]; }
    return 900000 + mvv + hist;
  }
  if(m==killers.moves[0]) return 800000;
  if(m==killers.moves[1]) return 750000;
  if(m==counter) return 700000;
  int hist = h.get_main(pos.side_to_move(), m);
  // continuation history would boost here
  return hist;
}

Move MovePicker::next_move(){
  while(true){
    if(stage_==STAGE_TT){
      stage_=STAGE_CAPTURE;
      if(ttMove_ && pos_.is_pseudo_legal(ttMove_)) return ttMove_;
      continue;
    }
    if(stage_==STAGE_CAPTURE || stage_==STAGE_QUIET){
      if(count_==0){
        ExtMove buf[MAX_MOVES];
        GenType gt = qsearch_? CAPTURES : (stage_==STAGE_CAPTURE? CAPTURES : QUIETS);
        // For main search we actually need both phases: first generate captures, then quiets
        if(!qsearch_ && stage_==STAGE_CAPTURE){
          int n = generate(pos_, buf, CAPTURES);
          for(int i=0;i<n;++i){
            if(buf[i].move==ttMove_) continue;
            moves_[count_++] = buf[i];
            moves_[count_-1].value = score_move(pos_, buf[i].move, *hist_, killers_, counter_);
          }
          std::sort(moves_, moves_+count_, [](auto &a, auto &b){ return a.value>b.value; });
        } else if(!qsearch_ && stage_==STAGE_QUIET){
          int n = generate(pos_, buf, QUIETS);
          for(int i=0;i<n;++i){
            if(buf[i].move==ttMove_) continue;
            // skip captures already done
            moves_[count_++] = buf[i];
            moves_[count_-1].value = score_move(pos_, buf[i].move, *hist_, killers_, counter_);
          }
          std::sort(moves_, moves_+count_, [](auto &a, auto &b){ return a.value>b.value; });
        } else {
          int n = generate(pos_, buf, CAPTURES);
          for(int i=0;i<n;++i){
            if(buf[i].move==ttMove_) continue;
            if(!see_ge(pos_, buf[i].move, 0)) continue; // in qsearch prune bad captures?
            moves_[count_++] = buf[i];
            moves_[count_-1].value = score_move(pos_, buf[i].move, *hist_, killers_, counter_);
          }
          std::sort(moves_, moves_+count_, [](auto &a, auto &b){ return a.value>b.value; });
        }
      }
      if(idx_ < count_) return moves_[idx_++].move;
      // move to next stage
      if(qsearch_) { stage_=STAGE_DONE; return 0; }
      if(stage_==STAGE_CAPTURE){ stage_=STAGE_QUIET; count_=0; idx_=0; continue; }
      else { stage_=STAGE_DONE; return 0; }
    }
    return 0;
  }
}

} // namespace
