#include "nekoclaw/search/timeman.h"
#include <algorithm>

namespace nekoclaw::search {

void TimeMan::init(const Limits& lim, int ply, bool isWhite){
  limits_=lim;
  start_=std::chrono::steady_clock::now();
  if(lim.movetime){
    optimum_=lim.movetime - 50;
    maximum_=lim.movetime - 10;
    if(optimum_<10) optimum_=10;
    if(maximum_<10) maximum_=10;
    return;
  }
  if(lim.wtime || lim.btime){
    int myTime = isWhite? lim.wtime : lim.btime;
    int myInc  = isWhite? lim.winc  : lim.binc;
    int mtg = lim.movestogo? lim.movestogo : 30;
    if(myTime<=0) { optimum_=1000; maximum_=5000; return; }
    // Simple formula: use time/mtg + inc*0.6, clamp 5% to 80%
    int base = myTime / mtg;
    optimum_ = base + myInc/2;
    maximum_ = base*2 + myInc;
    // overhead 100ms
    optimum_ = std::max(10, optimum_ - 50);
    maximum_ = std::max(10, maximum_ - 20);
    if(optimum_ > myTime/3) optimum_ = myTime/3;
    if(maximum_ > myTime/2) maximum_ = myTime/2;
    // ply adjustment: go faster early?
    if(ply>40) { optimum_ = optimum_*3/2; maximum_= maximum_*3/2; }
    return;
  }
  optimum_=maximum_=0; // infinite unless depth/nodes limit
}

bool TimeMan::should_stop() const {
  if(limits_.infinite) return false;
  if(limits_.movetime || limits_.wtime || limits_.btime){
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start_).count();
    return elapsed >= optimum_;
  }
  return false;
}

int64_t TimeMan::elapsed_ms() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start_).count();
}

} // namespace
