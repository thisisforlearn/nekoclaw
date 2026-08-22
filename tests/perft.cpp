#include "nekoclaw/position.h"
#include "nekoclaw/movegen.h"
#include <iostream>

using namespace nekoclaw;

static uint64_t perft(Position& pos, int depth){
  if(depth==0) return 1;
  ExtMove list[MAX_MOVES];
  int n = generate(pos, list, pos.in_check()? EVASIONS : LEGAL);
  uint64_t nodes=0;
  for(int i=0;i<n;++i){
    Move m=list[i].move;
    if(!pos.is_legal(m)) continue;
    StateInfo si;
    pos.do_move(m, si);
    nodes += perft(pos, depth-1);
    pos.undo_move(m);
  }
  return nodes;
}

int main(){
  init_all();
  Position pos; pos.set_startpos();
  struct {int d; uint64_t expect;} cases[] = {
    {1, 20}, {2, 400}, {3, 8902}, {4, 197281}, {5, 4865609}
  };
  bool ok=true;
  for(auto &c: cases){
    uint64_t n=perft(pos, c.d);
    std::cout<<"perft "<<c.d<<" = "<<n<<" expect "<<c.expect<<" "<<(n==c.expect?"OK":"FAIL")<<"\n";
    if(n!=c.expect) ok=false;
  }
  return ok?0:1;
}
