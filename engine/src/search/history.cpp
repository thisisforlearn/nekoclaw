#include "nekoclaw/search/history.h"
#include <cstring>
#include <cstdlib>

namespace nekoclaw::search {

History g_history;

void History::clear(){
  memset(main,0,sizeof(main));
  memset(cont,0,sizeof(cont));
  memset(capture,0,sizeof(capture));
}

static inline void upd(int16_t& v, int bonus){
  int32_t nv = v + bonus - v*int(std::abs(bonus))/16384;
  if(nv> 16383) nv=16383;
  if(nv<-16383) nv=-16383;
  v = int16_t(nv);
}

void History::update_main(Color c, Move m, int bonus){
  Square from=move_from(m), to=move_to(m);
  upd(main[c][from][to], bonus);
}
void History::update_cont(Piece pc, Square to, Piece prevPc, Square prevTo, int bonus){
  if(prevPc==NO_PIECE || pc==NO_PIECE) return;
  upd(cont[prevPc][prevTo][pc][to], bonus);
}
void History::update_capture(PieceType pt, Piece cap, Square to, int bonus){
  if(cap==NO_PIECE) return;
  int &v = capture[pt][cap][to];
  v += bonus - v*abs(bonus)/16384;
  if(v>16383) v=16383;
  if(v<-16383) v=-16383;
}

void Killers::clear(){ moves[0]=moves[1]=0; }
void Killers::update(Move m){ if(m!=moves[0]){ moves[1]=moves[0]; moves[0]=m; } }

} // namespace
