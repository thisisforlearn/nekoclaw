#include "nekoclaw/position.h"
#include "nekoclaw/bitboard.h"
#include "nekoclaw/magic.h"
#include <sstream>
#include <cctype>
#include <random>
#include <cstring>

namespace nekoclaw {

HashKey Position::zobPiece[PIECE_NB][SQUARE_NB];
HashKey Position::zobCastle[16];
HashKey Position::zobEnPassant[8];
HashKey Position::zobSide;
bool Position::zobInit = false;

static uint64_t splitmix64(uint64_t &x){
  uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

void Position::init_zobrist(){
  if(zobInit) return;
  uint64_t seed = 0x123456789abcdef0ULL;
  for(int p=0;p<PIECE_NB;++p) for(int s=0;s<64;++s) zobPiece[p][s]=splitmix64(seed);
  for(int i=0;i<16;++i) zobCastle[i]=splitmix64(seed);
  for(int i=0;i<8;++i) zobEnPassant[i]=splitmix64(seed);
  zobSide=splitmix64(seed);
  zobInit=true;
}

void init_all(){
  Position::init_zobrist();
  init_bitboards();
}

void Position::clear(){
  init_zobrist();
  byColor_[WHITE]=byColor_[BLACK]=0;
  for(int i=0;i<PIECE_TYPE_NB;++i) byType_[i]=0;
  for(int s=0;s<64;++s) board_[s]=NO_PIECE;
  kingSq_[WHITE]=E1; kingSq_[BLACK]=E8;
  stm_=WHITE;
  gamePly_=0;
  st_=nullptr;
  history_.clear();
  moveHistory_.clear();
}

void Position::put_piece(Piece pc, Square s){
  board_[s]=pc;
  byColor_[color_of(pc)] |= square_bb(s);
  byType_[type_of(pc)] |= square_bb(s);
  if(type_of(pc)==KING) kingSq_[color_of(pc)]=s;
}

void Position::remove_piece(Square s){
  Piece pc=board_[s];
  if(pc==NO_PIECE) return;
  byColor_[color_of(pc)] &= ~square_bb(s);
  byType_[type_of(pc)] &= ~square_bb(s);
  board_[s]=NO_PIECE;
}

void Position::move_piece(Square from, Square to){
  Piece pc=board_[from];
  Bitboard fromBB=square_bb(from), toBB=square_bb(to);
  byColor_[color_of(pc)] ^= fromBB ^ toBB;
  byType_[type_of(pc)] ^= fromBB ^ toBB;
  board_[to]=pc;
  board_[from]=NO_PIECE;
  if(type_of(pc)==KING) kingSq_[color_of(pc)]=to;
}

void Position::update_keys(){
  HashKey k=0, pk=0;
  for(int s=0;s<64;++s){
    Piece pc=board_[s];
    if(pc!=NO_PIECE){
      k ^= zobPiece[pc][s];
      if(type_of(pc)==PAWN) pk ^= zobPiece[pc][s];
    }
  }
  if(st_->epSquare!=SQ_NONE) k ^= zobEnPassant[file_of(st_->epSquare)];
  k ^= zobCastle[st_->castling & 0xF];
  if(stm_==BLACK) k ^= zobSide;
  st_->key=k;
  st_->pawnKey=pk;
}

void Position::set_check_info(){
  Color us=stm_, them=opposite(us);
  Square ksq=kingSq_[us];
  st_->checkers = attackers_to(ksq) & pieces(them);
  for(int pt=0;pt<PIECE_TYPE_NB;++pt) st_->checkSquares[pt]=0;
  for(int c=0;c<2;++c){ st_->blockers[c]=0; st_->pinners[c]=0; }
  Bitboard occ=occupancy();
  auto processSliders = [&](Bitboard attackers){
    while(attackers){
      Square s=lsb(attackers); attackers&=attackers-1;
      if(!(kLine[ksq][s])) continue;
      Bitboard between = kBetween[ksq][s] & occ;
      int cnt=popcount(between);
      if(cnt==1){
        Square b=lsb(between);
        if(board_[b]!=NO_PIECE && color_of(board_[b])==us){
          st_->blockers[us] |= square_bb(b);
          st_->pinners[them] |= square_bb(s);
        }
      }
    }
  };
  processSliders(pieces(them, ROOK) | pieces(them, QUEEN));
  processSliders(pieces(them, BISHOP) | pieces(them, QUEEN));
}

bool Position::set(const std::string& fen, StateInfo* si){
  clear();
  std::istringstream ss(fen);
  std::string board, stm, castle, ep, hm, fm;
  if(!(ss>>board>>stm>>castle>>ep>>hm>>fm)) return false;

  // board: rank 8 to 1
  int rank=7, file=0;
  for(char c: board){
    if(c=='/'){ rank--; file=0; continue; }
    if(isdigit(c)){ file += c-'0'; continue; }
    if(file>=8||rank<0) return false;
    Piece pc=NO_PIECE;
    Color col = isupper(c)?WHITE:BLACK;
    char lc=tolower(c);
    PieceType pt=PIECE_TYPE_NB;
    if(lc=='p') pt=PAWN;
    else if(lc=='n') pt=KNIGHT;
    else if(lc=='b') pt=BISHOP;
    else if(lc=='r') pt=ROOK;
    else if(lc=='q') pt=QUEEN;
    else if(lc=='k') pt=KING;
    else return false;
    pc=make_piece(col,pt);
    Square s=make_square(file,rank);
    put_piece(pc,s);
    file++;
  }
  stm_ = (stm=="w"?WHITE:BLACK);
  int cast=0;
  if(castle!="-") for(char c: castle){
    if(c=='K') cast|=1;
    else if(c=='Q') cast|=2;
    else if(c=='k') cast|=4;
    else if(c=='q') cast|=8;
  }
  Square epSq=SQ_NONE;
  if(ep!="-"){
    int f=ep[0]-'a', r=ep[1]-'1';
    if(f>=0&&f<8&&r>=0&&r<8) epSq=make_square(f,r);
  }
  int rule50 = hm.empty()?0:std::stoi(hm);
  // fm ignored for gamePly
  // Setup state
  static thread_local StateInfo tmp;
  StateInfo* st = si?si:&tmp;
  st->castling=cast;
  st->epSquare=epSq;
  st->rule50=rule50;
  st->pliesFromNull=0;
  st->captured=NO_PIECE;
  st->lastMove=0;
  st_=st;
  update_keys();
  set_check_info();
  if(board_[kingSq_[WHITE]]!=W_KING || board_[kingSq_[BLACK]]!=B_KING) return false;
  return true;
}

std::string Position::fen() const {
  std::string s;
  for(int r=7;r>=0;--r){
    int empty=0;
    for(int f=0;f<8;++f){
      Square sq=make_square(f,r);
      Piece pc=board_[sq];
      if(pc==NO_PIECE) empty++;
      else{
        if(empty){ s+=char('0'+empty); empty=0; }
        char c='?';
        PieceType pt=type_of(pc);
        if(pt==PAWN) c='p';
        else if(pt==KNIGHT) c='n';
        else if(pt==BISHOP) c='b';
        else if(pt==ROOK) c='r';
        else if(pt==QUEEN) c='q';
        else if(pt==KING) c='k';
        if(color_of(pc)==WHITE) c=toupper(c);
        s+=c;
      }
    }
    if(empty) s+=char('0'+empty);
    if(r) s+='/';
  }
  s+= (stm_==WHITE?" w ":" b ");
  if(st_->castling==0) s+="-";
  else{
    if(st_->castling&1) s+='K';
    if(st_->castling&2) s+='Q';
    if(st_->castling&4) s+='k';
    if(st_->castling&8) s+='q';
  }
  s+=' ';
  if(st_->epSquare==SQ_NONE) s+="-";
  else{ s+=char('a'+file_of(st_->epSquare)); s+=char('1'+rank_of(st_->epSquare)); }
  s+=' '+std::to_string(st_->rule50);
  s+=' '+std::to_string(1 + gamePly_/2);
  return s;
}

void Position::set_startpos(){
  set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", nullptr);
}

Bitboard Position::attackers_to(Square s, Bitboard occ) const {
  Bitboard att=0;
  att |= pawn_attacks(WHITE,s) & pieces(BLACK, PAWN);
  att |= pawn_attacks(BLACK,s) & pieces(WHITE, PAWN);
  att |= knight_attacks(s) & (pieces(WHITE,KNIGHT)|pieces(BLACK,KNIGHT));
  att |= king_attacks(s) & (pieces(WHITE,KING)|pieces(BLACK,KING));
  att |= bishop_attacks(s, occ) & (pieces(WHITE,BISHOP)|pieces(WHITE,QUEEN)|pieces(BLACK,BISHOP)|pieces(BLACK,QUEEN));
  att |= rook_attacks(s, occ) & (pieces(WHITE,ROOK)|pieces(WHITE,QUEEN)|pieces(BLACK,ROOK)|pieces(BLACK,QUEEN));
  return att;
}

bool Position::is_attacked(Square s, Color by) const {
  Bitboard occ=occupancy();
  if(pawn_attacks(opposite(by), s) & pieces(by, PAWN)) return true;
  if(knight_attacks(s) & pieces(by, KNIGHT)) return true;
  if(king_attacks(s) & pieces(by, KING)) return true;
  if(bishop_attacks(s, occ) & (pieces(by,BISHOP)|pieces(by,QUEEN))) return true;
  if(rook_attacks(s, occ) & (pieces(by,ROOK)|pieces(by,QUEEN))) return true;
  return false;
}

int Position::bucket_index() const {
  int pc=popcount(occupancy());
  int b=(pc-1)/4;
  if(b<0) b=0;
  if(b>=8) b=7;
  return b;
}

void Position::do_move(Move m, StateInfo& newSt){
  // copy current to history
  history_.push_back(st_);
  moveHistory_.push_back(m);
  newSt=*st_;
  newSt.lastMove=m;
  newSt.rule50++;
  newSt.pliesFromNull++;
  // update keys incrementally? we recompute at end for simplicity (still fast)
  Square from=move_from(m);
  Square to=move_to(m);
  Piece pc=board_[from];
  Piece cap=board_[to];
  bool isEnPass = (m & MoveFlag::ENPASSANT);
  bool isCastle = (m & MoveFlag::CASTLING);
  bool isPromo = (m & MoveFlag::PROMOTION);
  PieceType promoPt = move_promo(m);

  // capture
  if(cap!=NO_PIECE){
    remove_piece(to);
    newSt.captured=cap;
    newSt.rule50=0;
  } else if(isEnPass){
    Square capSq = (stm_==WHITE? Square(to-8) : Square(to+8));
    cap=board_[capSq];
    remove_piece(capSq);
    newSt.captured=cap;
    newSt.rule50=0;
  } else {
    newSt.captured=NO_PIECE;
  }

  // pawn double push ep
  newSt.epSquare=SQ_NONE;
  if(type_of(pc)==PAWN){
    newSt.rule50=0;
    if(std::abs(int(to)-int(from))==16){
      Square ep = Square((from+to)/2);
      // only set if enemy pawn can capture
      Bitboard enemyPawns = pieces(opposite(stm_), PAWN);
      if(pawn_attacks(stm_, ep) & enemyPawns){
        newSt.epSquare=ep;
      }
    }
    if(isPromo){
      remove_piece(from);
      Piece promo = make_piece(stm_, promoPt);
      put_piece(promo, to);
    } else {
      move_piece(from,to);
    }
  } else if(isCastle){
    // king move + rook
    move_piece(from,to);
    if(to==G1){ move_piece(H1,F1); }
    else if(to==C1){ move_piece(A1,D1); }
    else if(to==G8){ move_piece(H8,F8); }
    else if(to==C8){ move_piece(A8,D8); }
  } else {
    move_piece(from,to);
  }

  // castling rights update
  int cr=newSt.castling;
  if(pc==W_KING) cr &= ~(1|2);
  else if(pc==B_KING) cr &= ~(4|8);
  else if(pc==W_ROOK){
    if(from==H1) cr &= ~1;
    else if(from==A1) cr &= ~2;
  } else if(pc==B_ROOK){
    if(from==H8) cr &= ~4;
    else if(from==A8) cr &= ~8;
  }
  if(cap==W_ROOK){
    if(to==H1) cr &= ~1;
    else if(to==A1) cr &= ~2;
  } else if(cap==B_ROOK){
    if(to==H8) cr &= ~4;
    else if(to==A8) cr &= ~8;
  }
  newSt.castling=cr;

  stm_=opposite(stm_);
  st_=&newSt;
  gamePly_++;
  update_keys();
  set_check_info();
}

void Position::undo_move(Move m){
  Square from=move_from(m);
  Square to=move_to(m);
  bool isEnPass = (m & MoveFlag::ENPASSANT);
  bool isCastle = (m & MoveFlag::CASTLING);
  bool isPromo = (m & MoveFlag::PROMOTION);
  // Capture must be read from current state BEFORE popping (st_ still points to the state after the move)
  Piece captured = st_->captured;
  // revert stm and history
  stm_=opposite(stm_);
  gamePly_--;
  st_=history_.back(); history_.pop_back();
  moveHistory_.pop_back();
  // board undo
  if(isPromo){
    remove_piece(to);
    Piece pawn = make_piece(stm_, PAWN);
    put_piece(pawn, from);
    if(captured!=NO_PIECE){
      put_piece(captured, to);
    }
  } else if(isCastle){
    move_piece(to, from);
    if(to==G1){ move_piece(F1,H1); }
    else if(to==C1){ move_piece(D1,A1); }
    else if(to==G8){ move_piece(F8,H8); }
    else if(to==C8){ move_piece(D8,A8); }
  } else if(isEnPass){
    move_piece(to, from);
    Square capSq = (stm_==WHITE? Square(to-8) : Square(to+8));
    if(captured!=NO_PIECE) put_piece(captured, capSq);
  } else {
    move_piece(to, from);
    if(captured!=NO_PIECE) put_piece(captured, to);
  }
}

void Position::do_null_move(StateInfo& newSt){
  history_.push_back(st_);
  moveHistory_.push_back(0);
  newSt=*st_;
  newSt.lastMove=0;
  newSt.epSquare=SQ_NONE;
  newSt.pliesFromNull=0;
  newSt.rule50++;
  stm_=opposite(stm_);
  st_=&newSt;
  gamePly_++;
  update_keys();
  set_check_info();
}

void Position::undo_null_move(){
  stm_=opposite(stm_);
  gamePly_--;
  st_=history_.back(); history_.pop_back();
  moveHistory_.pop_back();
}

bool Position::is_pseudo_legal(Move m) const {
  if(m==0) return false;
  Square from=move_from(m), to=move_to(m);
  if(from>=64||to>=64) return false;
  Piece pc=board_[from];
  if(pc==NO_PIECE || color_of(pc)!=stm_) return false;
  // capture own?
  if(board_[to]!=NO_PIECE && color_of(board_[to])==stm_) return false;
  // basic piece movement check via attacks would be needed; simplified: generate and check membership
  // We'll assume true if generation would emit it — caller should filter
  return true;
}

bool Position::is_legal(Move m) const {
  if(!is_pseudo_legal(m)) return false;
  Position p = *this;
  StateInfo st;
  p.do_move(m, st);
  Square ksq = p.king_square(opposite(p.side_to_move()));
  bool attacked = p.is_attacked(ksq, p.side_to_move());
  return !attacked;
}

bool Position::is_draw(int ply) const {
  if(st_->rule50>=100) return true;
  if(is_repetition(ply)) return true;
  // insufficient material
  if(popcount(occupancy())==2) return true; // K vs K
  // K vs K+B/N etc could be draw but not needed
  return false;
}

bool Position::is_repetition(int ply) const {
  int reps=0;
  HashKey cur=st_->key;
  int need=0;
  // Check history keys for repetition (need 2 occurrences)
  // Walk back through history_ comparing keys, stop at null move or 50 moves
  for(int i=(int)history_.size()-1; i>=0; --i){
    if(history_[i]->key==cur) reps++;
    if(reps>=2) return true;
    if(history_[i]->rule50==0) break; // cannot repeat beyond pawn move/capture that resets?
    // extended check: if plies from null large, continue
  }
  return false;
}

} // namespace nekoclaw
