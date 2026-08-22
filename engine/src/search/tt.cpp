#include "nekoclaw/search/tt.h"
#include <cstring>
#include <cstdlib>

namespace nekoclaw::search {

TranspositionTable g_tt;

void TranspositionTable::resize(size_t mb){
  size_t entries = (mb * 1024 * 1024) / sizeof(TTEntry);
  // round down to power of two for fast & mask? we use modulo, so not needed
  table_.assign(entries, TTEntry{});
  clear();
}

void TranspositionTable::clear(){
  for(auto &e: table_) e = TTEntry{};
  generation_=0;
}

void TranspositionTable::new_search(){
  if(++generation_==0) generation_=1;
}

TTEntry* TranspositionTable::probe(HashKey key, bool& found){
  if(table_.empty()){ found=false; return nullptr; }
  size_t idx = key % table_.size();
  TTEntry* e = &table_[idx];
  found = (e->key==key && e->bound!=BOUND_NONE);
  return e;
}

void TranspositionTable::store(HashKey key, Move move, Value value, Value eval, Depth depth, Bound bound, bool isPV){
  if(table_.empty()) return;
  size_t idx = key % table_.size();
  TTEntry* e = &table_[idx];
  // replace strategy: prefer deeper, PV, and newer generation
  bool replace = (e->bound==BOUND_NONE) || depth+2 >= e->depth || e->gen != generation_ || isPV;
  if(!replace) {
    // keep move if new has no move
    if(move) e->move=move;
    return;
  }
  e->key=key;
  if(move) e->move=move;
  e->value=value;
  e->eval=eval;
  e->depth=depth;
  e->bound=bound;
  e->gen=generation_;
  e->is_pv=isPV;
}

size_t TranspositionTable::hashfull() const {
  if(table_.empty()) return 0;
  size_t cnt=0;
  size_t check = std::min<size_t>(1000, table_.size());
  for(size_t i=0;i<check;++i) if(table_[i].bound!=BOUND_NONE) ++cnt;
  return cnt * 1000 / check;
}

} // namespace
