#include "nekoclaw/bench.h"
#include "nekoclaw/position.h"
#include "nekoclaw/search/search.h"
#include "nekoclaw/movegen.h"
#include <chrono>
#include <iostream>

namespace nekoclaw {

static uint64_t perft(Position& pos, int depth){
  if(depth==0) return 1;
  ExtMove list[MAX_MOVES];
  int n = generate(pos, list, pos.in_check()? EVASIONS : LEGAL);
  // filter legal
  uint64_t nodes=0;
  for(int i=0;i<n;++i){
    Move m=list[i].move;
    if(!pos.is_legal(m)) continue;
    StateInfo si;
    pos.do_move(m, si);
    uint64_t sub = perft(pos, depth-1);
    nodes+=sub;
    pos.undo_move(m);
  }
  return nodes;
}

void perft_bench(){
  Position pos; pos.set_startpos();
  std::cout<<"perft startpos\n";
  for(int d=1; d<=6; ++d){
    auto t0=std::chrono::steady_clock::now();
    uint64_t n=perft(pos, d);
    auto t1=std::chrono::steady_clock::now();
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    std::cout<<"perft "<<d<<" "<<n<<" "<<ms<<"ms\n";
  }
  // known: perft 6 startpos = 119060324
}

void bench(int depth){
  std::cout<<"NekoClaw bench depth "<<depth<<"\n";
  init_all();
  // Perft
  perft_bench();
  // Search bench on a few positions
  const char* fens[]={
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"
  };
  search::Searcher s;
  s.clear();
  search::g_tt.resize(32);
  for(auto fen: fens){
    Position pos; StateInfo si; pos.set(fen,&si);
    s.set_position(pos);
    search::Limits lim; lim.depth=depth;
    auto t0=std::chrono::steady_clock::now();
    Move best=s.think(lim, depth);
    auto t1=std::chrono::steady_clock::now();
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
    std::cout<<"fen "<<fen<<"\nbest "<<move_to_uci(best)<<" "<<ms<<"ms nodes "<< s.info().nodes <<"\n";
  }
}

} // namespace
