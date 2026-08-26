#include "nekoclaw/uci.h"
#include "nekoclaw/position.h"
#include "nekoclaw/movegen.h"
#include "nekoclaw/search/history.h"
#include <cctype>
#include <cstring>
#include <cstdlib>
#include "nekoclaw/nnue/network.h"
#include "nekoclaw/search/search.h"
#include "nekoclaw/search/tt.h"
#include "nekoclaw/nnue/weights.h"
#include "nekoclaw/bench.h"
#include "version.h"
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace nekoclaw {

UCIOptions g_options;

static Position g_pos;
static StateInfo g_state;

static std::string move_to_san(const Position& pos, Move m){
  Square from=move_from(m), to=move_to(m);
  Piece pc=pos.piece_on(from);
  PieceType pt=type_of(pc);
  bool isCap = pos.piece_on(to)!=NO_PIECE || (m & MoveFlag::ENPASSANT);
  bool isPromo = m & MoveFlag::PROMOTION;
  if(m & MoveFlag::CASTLING){
    return (file_of(to)>file_of(from) ? "O-O" : "O-O-O");
  }
  std::string s;
  if(pt!=PAWN){
    s += "NBRQK"[pt-1];
    // disambiguation: check if another same piece can also go to to
    // For simplicity, we add file if needed (not perfect but works for most)
    ExtMove list[MAX_MOVES];
    int n=generate(pos, list, LEGAL);
    int same=0;
    for(int i=0;i<n;++i){
      if(list[i].move==m) continue;
      if(!pos.is_legal(list[i].move)) continue;
      Move o=list[i].move;
      if(pos.piece_on(move_from(o))==pc && move_to(o)==to) same++;
    }
    if(same) s += char('a'+file_of(from));
    if(isCap) s += "x";
    s += char('a'+file_of(to));
    s += char('1'+rank_of(to));
  } else {
    if(isCap){
      s += char('a'+file_of(from));
      s += "x";
    }
    s += char('a'+file_of(to));
    s += char('1'+rank_of(to));
  }
  if(isPromo){
    s += "=";
    PieceType pr=move_promo(m);
    s += "NBRQ"[pr-1];
  }
  // check/mate not needed for matching
  return s;
}
static Move parse_move(const Position& pos, const std::string& str){
  if(str.size()<4) return 0;
  int ff=str[0]-'a', fr=str[1]-'1', tf=str[2]-'a', tr=str[3]-'1';
  if(ff<0||ff>=8||tf<0||tf>=8||fr<0||fr>=8||tr<0||tr>=8) return 0;
  Square from=make_square(ff,fr), to=make_square(tf,tr);
  PieceType promo=PIECE_TYPE_NB;
  if(str.size()>=5){
    char c=tolower(str[4]);
    if(c=='n') promo=KNIGHT;
    else if(c=='b') promo=BISHOP;
    else if(c=='r') promo=ROOK;
    else if(c=='q') promo=QUEEN;
  }
  // Find matching pseudo move
  ExtMove list[MAX_MOVES];
  int n = generate(pos, list, pos.in_check()? EVASIONS : LEGAL); (void)n;
  // Actually generate all pseudo then filter
  ExtMove all[MAX_MOVES];
  int cnt = generate(pos, all, LEGAL);
  // LEGAL already filtered? but we need to match
  for(int i=0;i<cnt;++i){
    Move m=all[i].move;
    if(move_from(m)==from && move_to(m)==to){
      if(promo!=PIECE_TYPE_NB){
        if(move_promo(m)!=promo) continue;
      } else {
        if(m & MoveFlag::PROMOTION) continue;
      }
      // Check additional flags like castle/enpass? move_from/to already distinguishes
      // but we need to ensure is_legal
      if(pos.is_legal(m)) return m;
    }
  }
  // Fallback: construct directly (for BOOK)
  if(promo!=PIECE_TYPE_NB) return make_promotion(from,to,promo);
  // Detect castle
  Piece pc=pos.piece_on(from);
  if(type_of(pc)==KING && std::abs(int(to)-int(from))==2) return make_move(from,to,MoveFlag::CASTLING);
  // Detect en passant
  if(type_of(pc)==PAWN && to==pos.state()->epSquare && pos.piece_on(to)==NO_PIECE) return make_move(from,to,MoveFlag::ENPASSANT);
  // capture flag
  uint32_t flags=0;
  if(pos.piece_on(to)!=NO_PIECE) flags|=MoveFlag::CAPTURE;
  return make_move(from,to,flags);
}

static void handle_position(std::istringstream& is){
  std::string token;
  is>>token;
  if(token=="startpos"){
    g_pos.set_startpos();
    // reset state pointer
  } else if(token=="fen"){
    std::string fen;
    std::string part;
    // fen is 6 tokens
    for(int i=0;i<6;++i){ if(is>>part) fen += part + (i<5?" ":""); }
    g_pos.set(fen, &g_state);
  }
  // moves
  if(is>>token){
    if(token=="moves"){
      while(is>>token){
        Move m=parse_move(g_pos, token);
        if(!m) break;
        StateInfo ns;
        // Need to allocate new state per move; we use heap? Simplify: use static vector
        static std::vector<StateInfo> hist;
        hist.push_back(ns);
        // but we need separate storage per move; use thread_local stack
        // For UCI we just push onto a vector of StateInfo owned by position's history_
        // g_pos.do_move will copy into provided ns which lives on stack until next move? Need persistent.
        // Hack: allocate on heap and leak (fine for game)
        StateInfo* heap = new StateInfo(ns);
        g_pos.do_move(m, *heap);
      }
    }
  }
  search::g_searcher.set_position(g_pos);
}

static void handle_go(std::istringstream& is){
  search::Limits lim;
  std::string token;
  while(is>>token){
    if(token=="depth") is>>lim.depth;
    else if(token=="nodes") is>>lim.nodes;
    else if(token=="movetime") is>>lim.movetime;
    else if(token=="wtime") is>>lim.wtime;
    else if(token=="btime") is>>lim.btime;
    else if(token=="winc") is>>lim.winc;
    else if(token=="binc") is>>lim.binc;
    else if(token=="movestogo") is>>lim.movestogo;
    else if(token=="infinite") lim.infinite=true;
    else if(token=="ponder") lim.ponder=true;
  }
  Move best = search::g_searcher.think(lim);
  std::cout << "bestmove " << move_to_uci(best) << std::endl;
}

void uci_loop(){
  init_all();
  g_pos.set_startpos();
  search::g_searcher.set_position(g_pos);
  search::g_tt.resize(g_options.hashMB);
  g_options.threads=1;

  std::string line;
  while(std::getline(std::cin, line)){
    std::istringstream is(line);
    std::string cmd; is>>cmd;
    if(cmd=="uci"){
      std::cout << "id name " << "NekoClaw v1.0.0" << " by Vaibhav\n";
      std::cout << "id author Vaibhav\n";
      std::cout << "option name Hash type spin default 128 min 1 max 1024\n";
      std::cout << "option name Threads type spin default 1 min 1 max 64\n";
      std::cout << "option name NNUEFile type string default weights/nekoclaw-1024x8-scReLU.nnue\n";
      std::cout << "option name Ponder type check default true\n";
      std::cout << "uciok\n";
    } else if(cmd=="isready"){
      std::cout << "readyok\n";
    } else if(cmd=="ucinewgame"){
      search::g_tt.clear();
      search::g_history.clear();
      g_pos.set_startpos();
      search::g_searcher.clear();
      search::g_searcher.set_position(g_pos);
    } else if(cmd=="position"){
      handle_position(is);
    } else if(cmd=="go"){
      handle_go(is);
    } else if(cmd=="stop"){
      search::g_searcher.stop();
    } else if(cmd=="quit"){
      break;
    } else if(cmd=="bench"){
      int depth=14; if(is>>depth) {} bench(depth);
    } else if(cmd=="d"){
      std::cout << g_pos.fen() << "\n";
      std::cout << (g_pos.in_check()?"in check\n":"");
      // print board
      for(int r=7;r>=0;--r){
        for(int f=0;f<8;++f){
          Piece pc=g_pos.piece_on(make_square(f,r));
          char c='.';
          if(pc!=NO_PIECE){ PieceType pt=type_of(pc); const char* s="pnbrqk"; c=s[pt]; if(color_of(pc)==WHITE) c=toupper(c); }
          std::cout<<c<<' ';
        }
        std::cout<< (r+1)<< "\n";
      }
      std::cout<<"a b c d e f g h\n";
    } else if(cmd=="eval"){
      // eval display placeholder
      // placeholder
      std::cout << "eval not impl in uci eval, use bench\n";
    } else if(cmd=="setoption"){
      std::string name, value;
      is>>name; // should be "name"
      is>>name;
      if(name=="Hash"){
        is>>value; is>>value; int mb=128; mb=std::atoi(value.c_str()); if(mb<=0) mb=128;
        g_options.hashMB=mb; search::g_tt.resize(mb);
      } else if(name=="NNUEFile"){
        std::string dummy; is>>dummy; std::string path; std::getline(is, path);
        // trim
        size_t p=path.find_first_not_of(" \t");
        if(p!=std::string::npos) path=path.substr(p);
        else path="";
        if(path.empty()){
          // fallback: path may have been read as dummy? try to handle
        }
        g_options.nnueFile=path;
        std::string err;
        if(!nnue::load_weights(path, err)) std::cout << "info string NNUE load failed: " << err << "\n";
        else std::cout << "info string NNUE loaded: " << path << "\n";
      } else if(name=="Threads"){
        is>>value; is>>value; int th=1; th=std::atoi(value.c_str()); if(th<=0) th=1;
        g_options.threads=th;
      }
    }
  }
}

static void print_board(const Position& pos){
  // Pretty board with Unicode and coordinates, from White perspective
  const char* uni[12]={"♟","♞","♝","♜","♛","♚","♙","♘","♗","♖","♕","♔"};
  const char* reset="\033[0m";
  const char* darkBg="\033[48;5;239m";
  const char* lightBg="\033[48;5;250m";
  std::cout<<"\n   a b c d e f g h\n";
  for(int r=7;r>=0;--r){
    std::cout<<" "<<(r+1)<<" ";
    for(int f=0;f<8;++f){
      Square sq=make_square(f,r);
      Piece pc=pos.piece_on(sq);
      bool isDark = (f+r)%2==0;
      std::cout<<(isDark?darkBg:lightBg);
      if(pc==NO_PIECE) std::cout<<"  ";
      else std::cout<<uni[pc]<<" ";
      std::cout<<reset;
    }
    std::cout<<" "<<(r+1)<<"\n";
  }
  std::cout<<"   a b c d e f g h\n";
  std::cout<<"FEN: "<<pos.fen()<<(pos.in_check()?"  + check":"")<<"\n";
  Value v=nnue::evaluate(pos);
  std::cout<<"Eval: "<<v<<" cp  Side: "<<(pos.side_to_move()==WHITE?"White":"Black")<<"\n";
}
void console_loop(){
  init_all();
  Position pos; pos.set_startpos();
  search::g_searcher.set_position(pos);
  search::g_tt.resize(128);
  std::cout << "🐾 NekoClaw v1.0.0 — the cutest chess engine by Vaibhav\n";
  std::cout << "Type 'help' for help, 'exit' to quit. Moves can be SAN (e4, Nf3, O-O) or UCI (e2e4)\n";
  print_board(pos);
  std::string line;
  while(true){
    std::cout << (pos.side_to_move()==WHITE?"White> ":"Black> ");
    if(!std::getline(std::cin, line)) break;
    if(line=="exit"||line=="quit") break;
    if(line=="help"){
      std::cout << "Commands: startpos, fen <fen>, e4/Nf3/O-O (SAN) or e2e4 (UCI), go depth <n>, undo, eval, d, perft, bench, help\n";
      continue;
    }
    if(line.rfind("fen ",0)==0){
      std::string fen=line.substr(4);
      StateInfo si;
      if(pos.set(fen,&si)) search::g_searcher.set_position(pos);
      else std::cout<<"bad fen\n";
      continue;
    }
    if(line=="startpos"){ pos.set_startpos(); search::g_searcher.set_position(pos); continue; }
    // accept SAN (e4, Nf3, O-O) or UCI (e2e4) or "move e2e4"
    std::string mv = line;
    if(mv.rfind("move ",0)==0) mv = mv.substr(5);
    while(!mv.empty() && isspace(mv.front())) mv.erase(mv.begin());
    while(!mv.empty() && isspace(mv.back())) mv.pop_back();
    // try UCI first, then SAN
    if(!mv.empty()){
      Move m=0;
      // UCI 4-5 chars
      if(mv.size()>=4 && mv[0]>='a' && mv[0]<='h' && mv[1]>='1' && mv[1]<='8' && mv[2]>='a' && mv[2]<='h' && mv[3]>='1' && mv[3]<='8'){
        m=parse_move(pos, mv.substr(0,5));
      }
      if(!m){
        // Try SAN: generate all legal and match SAN
        ExtMove list[MAX_MOVES];
        int n=generate(pos, list, LEGAL);
        for(int i=0;i<n;++i){
          Move cand=list[i].move;
          if(!pos.is_legal(cand)) continue;
          std::string san=move_to_san(pos, cand);
          std::string uci=move_to_uci(cand);
          // case-insensitive match for SAN, also allow "e4" vs "e4+" etc.
          std::string mvLow=mv, sanLow=san;
          for(char &c: mvLow) c=tolower(c);
          for(char &c: sanLow) c=tolower(c);
          if(mvLow==sanLow || mvLow==uci){
            m=cand; break;
          }
          // also allow without 'x' for pawn captures? try file
          if(sanLow.find(mvLow)!=std::string::npos) {
            // fallback: check if mv is substring of SAN (e.g., "e4" matches "e4")
          }
        }
        // fallback for short pawn like "e4" -> find pawn move to e4
        if(!m && mv.size()==2 && mv[0]>='a' && mv[0]<='h' && mv[1]>='1' && mv[1]<='8'){
          Square to=make_square(mv[0]-'a', mv[1]-'1');
          ExtMove list2[MAX_MOVES];
          int n2=generate(pos, list2, LEGAL);
          for(int i=0;i<n2;++i){
            Move cand=list2[i].move;
            if(!pos.is_legal(cand)) continue;
            if(move_to(cand)==to && type_of(pos.piece_on(move_from(cand)))==PAWN){
              m=cand; break;
            }
          }
        }
      }
      if(m){
        std::string san=move_to_san(pos, m);
        std::string uci=move_to_uci(m);
        StateInfo* heap=new StateInfo(); pos.do_move(m,*heap); search::g_searcher.set_position(pos);
        std::cout<<"played "<<san<<" ("<<uci<<")\n";
        print_board(pos);
        continue;
      }
    }
    if(line.rfind("move ",0)==0){
      std::string mv=line.substr(5);
      Move m=parse_move(pos,mv);
      if(!m) std::cout<<"illegal "<<mv<<"\n";
      else { StateInfo* heap=new StateInfo(); pos.do_move(m,*heap); search::g_searcher.set_position(pos); std::cout<<"played "<<mv<<"\n"; }
      continue;
    }
    if(line.rfind("go",0)==0){
      std::istringstream is(line);
      std::string c; is>>c;
      search::Limits lim; int d=8; if(is>>c){ if(c=="depth") is>>d; }
      lim.depth=d;
      Move best=search::g_searcher.think(lim);
      std::cout<<"best "<<move_to_uci(best)<<"\n";
      continue;
    }
    if(line=="undo"){ /* not tracked */ std::cout<<"undo not impl in console yet\n"; continue; }
    if(line=="d"){ print_board(pos); continue; }
    if(line.rfind("perft",0)==0){
      int d=5; d=std::atoi(line.substr(6).c_str()); if(d<=0) d=5;
      // call perft via bench?
      std::cout<<"perft not impl yet\n";
      continue;
    }
    if(line=="eval"){
      Value v = nnue::evaluate(pos);
      std::cout<<"eval "<<v<<" cp\n";
      continue;
    }
    if(line=="bench"){ bench(14); continue; }
    std::cout<<"unknown\n";
  }
}

} // namespace
