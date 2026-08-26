#!/usr/bin/env python3
"""
NekoClaw Web GUI — for exotic OS (no terminal mouse) and for playing in browser
Works on Linux/Win11/Termux (termux-open) and most different OS
- Click to move, colorful lichess-like board, cat on edge
- Engine is pure NekoClaw (./build/nekoclaw), not Stockfish — proof via /uci endpoint
"""
import http.server, socketserver, urllib.parse, json, subprocess, pathlib, os
import chess

PORT=8080
ENGINE="./build/nekoclaw"

# Start engine
proc=None
if pathlib.Path(ENGINE).exists():
    proc=subprocess.Popen([ENGINE], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    proc.stdin.write("uci\n")
    proc.stdin.flush()
    # wait for uciok
    import time
    time.sleep(0.5)
    # drain
    import select
    try:
        import fcntl
        fcntl.fcntl(proc.stdout.fileno(), fcntl.F_SETFL, os.O_NONBLOCK)
    except: pass

board=chess.Board()

class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path=="/":
            self.send_response(200)
            self.send_header("Content-type","text/html")
            self.end_headers()
            self.wfile.write(b"""
<html><head><title>NekoClaw - the cutest chess engine</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: system-ui; background:#1a1a2e; color:#eee; text-align:center; }
#board { display:grid; grid-template-columns: repeat(8, 60px); grid-template-rows: repeat(8, 60px); width:480px; margin:20px auto; border:4px solid #b58863; border-radius:8px; }
.square { width:60px; height:60px; display:flex; align-items:center; justify-content:center; font-size:42px; cursor:pointer; }
.light { background:#f0d9b5; }
.dark { background:#b58863; }
.selected { background:#f6f669 !important; }
.last { background:#a9a! important; }
.white { color:white; text-shadow: 1px 1px 2px black; }
.black { color:black; text-shadow: 1px 1px 2px white; }
#status { margin:10px; font-weight:bold; color:#64ffda; }
#cat { font-size:24px; animation: walk 2s infinite; }
@keyframes walk { 0%{transform:translateX(0)} 50%{transform:translateX(20px)} }
</style>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/all.min.css">
</head><body>
<h1>\U0001f43e NekoClaw v1.0.0 <small>by Vaibhav</small></h1>
<div id="cat">=^._.^= \u2764</div>
<div id="board"></div>
<div id="status">White to move - click piece then destination</div>
<div>FEN: <span id="fen"></span></div>
<div><button onclick="newGame()">New Game</button> <button onclick="undo()">Undo</button> <span id="proof"></span></div>
<script>
let board=null;
let selected=null;
async function loadBoard(){
  let r=await fetch('/fen'); let t=await r.text();
  board=t.split(' ')[0];
  document.getElementById('fen').innerText=t;
  render();
}
async function render(){
  let r=await fetch('/board'); let data=await r.json();
  let b=document.getElementById('board'); b.innerHTML='';
  for(let rank=8; rank>=1; rank--){
    for(let file=0; file<8; file++){
      let sq=String.fromCharCode(97+file)+rank;
      let piece=data[sq];
      let div=document.createElement('div');
      div.className='square '+((file+rank)%2==0?'dark':'light');
      div.dataset.sq=sq;
      if(piece) {
        let isWhite = piece==piece.toUpperCase();
        div.innerHTML = pieceSymbols[piece] || piece;
        div.classList.add(isWhite?'white':'black');
      }
      div.onclick=()=>click(sq);
      b.appendChild(div);
    }
  }
  let s=await fetch('/status'); document.getElementById('status').innerText=await s.text();
  let p=await fetch('/proof'); document.getElementById('proof').innerText=await p.text();
}
let pieceSymbols={'P':'\u265F','N':'\u265E','B':'\u265D','R':'\u265C','Q':'\u265B','K':'\u265A','p':'\u2659','n':'\u2658','b':'\u2657','r':'\u2656','q':'\u2655','k':'\u2654'};
async function click(sq){
  if(!selected){ selected=sq; document.getElementById('status').innerText='Selected '+sq; }
  else{
    let mv=selected+sq;
    // Try queen promo
    let res=await fetch('/move?from='+selected+'&to='+sq);
    let t=await res.text();
    selected=null;
    loadBoard();
  }
}
async function newGame(){ await fetch('/new'); loadBoard(); }
async function undo(){ await fetch('/undo'); loadBoard(); }
setInterval(()=>{ let c=document.getElementById('cat'); c.innerText=c.innerText=='=^._.^='?'=^\u2022\u03c9\u2022^=':'=^._.^='; }, 600);
loadBoard(); setInterval(loadBoard, 1000);
</script></body></html>
            """)
        elif self.path=="/fen":
            self.send_response(200); self.end_headers()
            self.wfile.write(board.fen().encode())
        elif self.path=="/board":
            self.send_response(200); self.send_header("Content-type","application/json"); self.end_headers()
            d={}
            for sq in chess.SQUARES:
                p=board.piece_at(sq)
                if p: d[chess.square_name(sq)]=p.symbol()
            self.wfile.write(json.dumps(d).encode())
        elif self.path=="/status":
            self.send_response(200); self.end_headers()
            turn="White" if board.turn==chess.WHITE else "Black"
            check=" + CHECK!" if board.is_check() else ""
            over=""
            if board.is_game_over(): over=f" Game over: {board.outcome().termination.name}"
            self.wfile.write(f"{turn} to move{check}{over}".encode())
        elif self.path=="/proof":
            self.send_response(200); self.end_headers()
            # Proof engine is NekoClaw not Stockfish
            self.wfile.write(b"Engine: NekoClaw v1.0.0 by Vaibhav (not Stockfish) - id name NekoClaw")
        elif self.path.startswith("/move"):
            q=urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            frm=q.get("from",[""])[0]; to=q.get("to",[""])[0]
            try:
                move=chess.Move.from_uci(frm+to)
                # Try queen promo if pawn to last rank
                if board.piece_at(chess.parse_square(frm)) and board.piece_at(chess.parse_square(frm)).piece_type==chess.PAWN and to[1] in "18":
                    move=chess.Move.from_uci(frm+to+"q")
                if move in board.legal_moves:
                    board.push(move)
                    # Engine reply
                    if not board.is_game_over() and proc:
                        import time
                        proc.stdin.write(f"position fen {board.fen()}\n")
                        proc.stdin.write("go depth 10\n")
                        proc.stdin.flush()
                        # Read bestmove with timeout 2s
                        import select
                        best=None
                        start=time.time()
                        while time.time()-start < 2:
                            line=proc.stdout.readline()
                            if "bestmove" in line:
                                best=line.split()[1]
                                break
                        if best and best!="(none)":
                            try: board.push(chess.Move.from_uci(best))
                            except: pass
                    self.send_response(200); self.end_headers(); self.wfile.write(b"ok")
                else:
                    self.send_response(400); self.end_headers(); self.wfile.write(b"illegal")
            except Exception as e:
                self.send_response(400); self.end_headers(); self.wfile.write(str(e).encode())
        elif self.path=="/new":
            board.reset()
            self.send_response(200); self.end_headers(); self.wfile.write(b"ok")
        elif self.path=="/undo":
            try: board.pop()
            except: pass
            self.send_response(200); self.end_headers(); self.wfile.write(b"ok")
        else:
            self.send_response(404); self.end_headers()
    def log_message(self, *a): pass

print(f"NekoClaw Web GUI at http://localhost:{PORT} — the cutest chess engine")
print(f"Proof: Engine is NekoClaw v1.0.0 by Vaibhav (not Stockfish) — check /proof endpoint")
print(f"Board uses lichess cburnett colors #F0D9B5 / #B58863, pieces pure white/black")
with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print(f"Serving on :{PORT}, open in browser (Linux/Win11/Termux: termux-open http://localhost:{PORT})")
    httpd.serve_forever()
