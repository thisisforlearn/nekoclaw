#include "nekoclaw/search/search.h"
#include "nekoclaw/search/movepick.h"
#include "nekoclaw/nnue/network.h"
#include "nekoclaw/movegen.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <chrono>

namespace nekoclaw::search {

Searcher g_searcher;

Searcher::Searcher(){
  tt_=&g_tt;
  hist_=&g_history;
}

void Searcher::clear(){
  tt_->clear();
  hist_->clear();
  for(auto &s: stack_) s = SearchStack{};
}

void Searcher::set_position(const Position& pos){
  rootPos_=pos;
}

// Evaluate with NNUE + tempo + small tweaks
Value Searcher::evaluate(const Position& pos){
  Value v = nnue::evaluate(pos);
  // tempo bonus 10cp for side to move
  v += 10;
  return v;
}

bool Searcher::should_prune(int depth, Value staticEval, Value beta) const {
  // futility pruning at shallow depths
  if(depth<=2 && staticEval+200*depth < beta) return true;
  return false;
}

// Quiescence: only captures and check evasions
Value Searcher::quiescence(Value alpha, Value beta, int ply, SearchStack* ss){
  info_.nodes++;
  if(ply>=MAX_PLY) return evaluate(rootPos_);
  // probe TT for qsearch? simplified: not here
  bool inCheck = rootPos_.in_check();
  Value best = VALUE_MATED;
  if(!inCheck){
    Value eval = evaluate(rootPos_);
    ss->staticEval=eval;
    if(eval >= beta) return eval;
    if(eval > alpha) alpha=eval;
    best=eval;
  }

  MovePicker mp(rootPos_, 0, 0, *hist_, Killers{}, 0, true);
  // In qsearch we also consider promotions and en passant
  while(Move m = mp.next_move()){
    // SEE prune in qsearch
    if(!inCheck && !see_ge(rootPos_, m, 0)) continue;
    // legality (filtered in movepick? we generated captures only, but need pin legality)
    if(!rootPos_.is_legal(m)) continue;

    StateInfo ns;
    rootPos_.do_move(m, ns);
    ss[1].ply=ply+1;
    Value val = -quiescence(-beta, -alpha, ply+1, ss+1);
    rootPos_.undo_move(m);

    if(info_.stop) return 0;
    if(val > best) best=val;
    if(val > alpha){
      alpha=val;
      if(alpha>=beta) break;
    }
  }
  return best;
}

// Negamax with PVS, TT, LMR, NMP, etc.
Value Searcher::negamax(int depth, Value alpha, Value beta, int ply, bool isPV, SearchStack* ss){
  if(info_.stop) return 0;
  info_.nodes++;
  ss->ply=ply;
  if(ply>=MAX_PLY) return evaluate(rootPos_);
  bool inCheck = rootPos_.in_check();

  // TT probe
  HashKey key = rootPos_.key();
  bool ttHit=false;
  TTEntry* tte = tt_->probe(key, ttHit);
  Move ttMove = ttHit? tte->move:0;
  Value ttValue = ttHit? tte->value:VALUE_DRAW;
  Depth ttDepth = ttHit? tte->depth: -100;

  // depth 0 => qsearch
  if(depth<=0 && !inCheck){
    return quiescence(alpha, beta, ply, ss);
  }

  // mate distance pruning
  alpha = std::max(alpha, mated_in(ply));
  beta  = std::min(beta, mate_in(ply+1));
  if(alpha>=beta) return alpha;

  // is draw?
  if(rootPos_.is_draw(ply)) return VALUE_DRAW;

  // TT cutoff
  if(!isPV && ttHit && ttDepth>=depth){
    Bound b = tte->bound;
    if(b==BOUND_EXACT) return ttValue;
    if(b==BOUND_LOWER && ttValue>=beta) return ttValue;
    if(b==BOUND_UPPER && ttValue<=alpha) return ttValue;
  }

  Value staticEval = VALUE_DRAW;
  if(!inCheck){
    staticEval = tte && ttHit && tte->eval!=VALUE_DRAW ? tte->eval : evaluate(rootPos_);
    ss->staticEval=staticEval;
  } else {
    ss->staticEval = -VALUE_INFINITE;
  }

  // Null move pruning
  if(!isPV && !inCheck && depth>=3 && staticEval >= beta && !rootPos_.is_draw(0)){
    // can do null move if not in check and we have enough material
    if(rootPos_.piece_count() > 7){ // avoid zugzwang in pawn endgames
      StateInfo ns;
      rootPos_.do_null_move(ns);
      ss->currentMove=0;
      Value v = -negamax(depth-3-(depth/4), -beta, -beta+1, ply+1, false, ss+1);
      rootPos_.undo_null_move();
      if(v>=beta) return beta; // null move cutoff
    }
  }

  // Razoring / futility?
  // We'll do simple futility at depth 1-2

  MovePicker mp(rootPos_, ttMove, depth, *hist_, Killers{ss->killers[0], ss->killers[1]}, ss->counter, false);
  Move bestMove=0;
  Value bestValue=-VALUE_INFINITE;
  int moveCount=0;
  Bound bound=BOUND_UPPER;
  Value origAlpha=alpha;

  // PVS loop
  while(Move m = mp.next_move()){
    if(!rootPos_.is_legal(m)) continue;
    moveCount++;
    // LMR
    int newDepth = depth-1;
    bool isCapture = (m & MoveFlag::CAPTURE) || (m & MoveFlag::PROMOTION);
    bool isKiller = (m==ss->killers[0] || m==ss->killers[1]);
    // Late move reductions
    int reduction=0;
    if(depth>=3 && moveCount>3 && !isCapture && !inCheck && !isKiller && !isPV){
      int histScore = hist_->get_main(rootPos_.side_to_move(), m);
      reduction = 1 + (moveCount>8?1:0) + (histScore<0?1:0) - (histScore>2000?1:0);
      reduction = std::clamp(reduction, 0, newDepth-1);
    }

    // save for history bonus later
    Piece moved = rootPos_.piece_on(move_from(m));
    StateInfo ns;
    rootPos_.do_move(m, ns);
    ss->currentMove=m;
    ss->movedPiece = moved;
    ss[1].prevMove=m;
    Value val;
    if(moveCount==1){
      val = -negamax(newDepth, -beta, -alpha, ply+1, isPV, ss+1);
    } else {
      int rDepth = newDepth - reduction;
      if(rDepth<0) rDepth=0;
      val = -negamax(rDepth, -alpha-1, -alpha, ply+1, false, ss+1);
      if(val>alpha && reduction>0){
        val = -negamax(newDepth, -alpha-1, -alpha, ply+1, false, ss+1);
      }
      if(val>alpha && val<beta && isPV){
        val = -negamax(newDepth, -beta, -alpha, ply+1, true, ss+1);
      }
    }
    rootPos_.undo_move(m);

    if(info_.stop) return 0;
    if(val>bestValue){
      bestValue=val;
      bestMove=m;
      if(val>alpha){
        alpha=val;
        bound=BOUND_EXACT;
        // update PV
        ss->pv[0]=m;
        for(int i=0;i<ss[1].pvLength;++i) ss->pv[i+1]=ss[1].pv[i];
        ss->pvLength = ss[1].pvLength+1;
        if(alpha>=beta){
          bound=BOUND_LOWER;
          break;
        }
      }
    }
    // history update on beta cutoff
    if(alpha>=beta){
      if(!isCapture){
        hist_->update_main(rootPos_.side_to_move(), m, depth*depth);
        ss->killers[1]=ss->killers[0];
        ss->killers[0]=m;
      } else {
        // capture history: generic boost (no piece-specific)
        // hist_->update_capture(type_of(moved), Piece(NO_PIECE), move_to(m), depth*depth/2);
      }
      break;
    }
  }

  if(moveCount==0){
    if(inCheck) return mated_in(ply);
    else return VALUE_DRAW; // stalemate
  }

  // store TT
  Value toStore = bestValue;
  // adjust mate scores for TT
  if(is_mate_score(toStore)) toStore = toStore >0 ? toStore + ply : toStore - ply;
  tt_->store(key, bestMove, toStore, staticEval, depth, bound, isPV);

  // quiet history penalty for non-best moves?
  if(bestMove && bound!=BOUND_LOWER){
    // bonus for best quiet
    if(!(bestMove & MoveFlag::CAPTURE)){
      hist_->update_main(rootPos_.side_to_move(), bestMove, depth*2);
    }
  }

  return bestValue;
}

Move Searcher::think(const Limits& limits, int maxDepth){
  limits_ = limits;
  tm_.init(limits, 0, rootPos_.side_to_move()==WHITE);
  info_.nodes=0; info_.stop=false; info_.depth=0; info_.selDepth=0;
  info_.start = std::chrono::steady_clock::now();
  tt_->new_search();
  // iterative deepening
  Value prevScore=VALUE_DRAW;
  Move bestMove=0;
  // aspiration window
  for(int depth=1; depth<=maxDepth; ++depth){
    if(tm_.should_stop() || tm_.should_stop_depth(depth)) break;
    Value delta=15;
    Value alpha=-VALUE_INFINITE, beta=VALUE_INFINITE;
    if(depth>=4){
      alpha = prevScore - delta;
      beta  = prevScore + delta;
    }
    Value score = negamax(depth, alpha, beta, 0, true, stack_);
    // aspiration fail low/high
    if(score<=alpha){
      alpha=-VALUE_INFINITE;
      beta=score+delta;
      score = negamax(depth, alpha, beta, 0, true, stack_);
    } else if(score>=beta){
      alpha=score-delta;
      beta=VALUE_INFINITE;
      score = negamax(depth, alpha, beta, 0, true, stack_);
    }
    prevScore=score;
    info_.depth=depth;
    info_.bestScore=score;
    // PV is in stack_[0]
    if(stack_[0].pvLength>0) bestMove=stack_[0].pv[0];
    else if(score!=0){
      // fallback: TT move
      bool hit=false; auto e=tt_->probe(rootPos_.key(), hit); if(hit) bestMove=e->move;
    }
    // time check
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-info_.start).count();
    // print UCI info
    std::cout << "info depth " << depth << " score ";
    if(is_mate_score(score)) std::cout << "mate " << (score>0? (VALUE_MATE-score+1)/2 : -(VALUE_MATE+score+1)/2);
    else std::cout << "cp " << score;
    std::cout << " nodes " << info_.nodes << " nps " << (info_.nodes*1000/std::max<int64_t>(1,elapsed));
    std::cout << " hashfull " << tt_->hashfull();
    std::cout << " time " << elapsed << " pv";
    for(int i=0;i<stack_[0].pvLength;++i) std::cout << " " << move_to_uci(stack_[0].pv[i]);
    std::cout << std::endl;
    if(tm_.should_stop()) break;
    if(score==mated_in(0) || score==mated_in(1)) break;
    if(limits.depth && depth>=limits.depth) break;
    if(limits.nodes && info_.nodes>=limits.nodes) break;
  }
  info_.bestMove=bestMove;
  return bestMove;
}

} // namespace
