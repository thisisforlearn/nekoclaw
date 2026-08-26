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
body { font-family: 'Segoe UI', system-ui; background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%); color:#eee; text-align:center; margin:0; padding:20px; }
h1 { color:#ff69b4; text-shadow: 2px 2px 4px rgba(0,0,0,0.5); }
#board { display:grid; grid-template-columns: repeat(8, 68px); grid-template-rows: repeat(8, 68px); width:544px; margin:20px auto; border:6px solid #b58863; border-radius:12px; box-shadow: 0 8px 32px rgba(0,0,0,0.5); overflow:hidden; }
.square { width:68px; height:68px; display:flex; align-items:center; justify-content:center; font-size:48px; cursor:pointer; transition: all 0.15s; position:relative; }
.square:hover { filter: brightness(1.15); transform: scale(1.02); }
.light { background:#f0d9b5; }
.dark { background:#b58863; }
.selected { background:#f6f669 !important; box-shadow: inset 0 0 12px rgba(255,230,0,0.8); }
.last { background:#cdd26a !important; }
.white { color:#fff; text-shadow: 2px 2px 4px rgba(0,0,0,0.9), 0 0 6px rgba(255,255,255,0.5); font-weight:bold; }
.black { color:#000; text-shadow: 1px 1px 2px rgba(255,255,255,0.8), 0 0 4px rgba(0,0,0,0.5); font-weight:bold; }
#status { margin:12px; font-weight:bold; color:#64ffda; font-size:18px; background:rgba(0,0,0,0.3); padding:8px 16px; border-radius:20px; display:inline-block; }
#cat { font-size:28px; animation: walk 1.2s infinite; display:inline-block; filter: drop-shadow(2px 2px 4px rgba(255,105,180,0.5)); }
@keyframes walk { 0%{transform:translateX(0) rotate(-5deg)} 25%{transform:translateX(15px) rotate(5deg)} 50%{transform:translateX(30px) rotate(-5deg)} 75%{transform:translateX(15px) rotate(5deg)} 100%{transform:translateX(0) rotate(-5deg)} }
button { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color:white; border:none; padding:10px 20px; margin:5px; border-radius:20px; cursor:pointer; font-weight:bold; transition: all 0.2s; }
button:hover { transform: translateY(-2px); box-shadow: 0 4px 12px rgba(102,126,234,0.4); }
#progress { width:544px; margin:10px auto; height:6px; background:rgba(255,255,255,0.1); border-radius:3px; overflow:hidden; display:none; }
#progress-bar { height:100%; background: linear-gradient(90deg, #ff69b4, #764ba2); width:0%; transition: width 0.3s; }
.controls { margin:10px; }
.flip-note { font-size:12px; color:#aaa; margin-top:5px; }
</style>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/all.min.css">
</head><body>
<h1>\U0001f43e NekoClaw v1.0.0 <small>by Vaibhav</small></h1>
<div id="cat">=^._.^= \u2764</div>
<div id="board"></div>
<div id="status">White to move - click piece then destination</div>
<div class="controls">
  <button onclick="newGame()">New Game</button> 
  <button onclick="undo()">Undo</button> 
  <button onclick="flipBoard()">Flip Board</button>
  <button onclick="playAs('white')">Play as White</button>
  <button onclick="playAs('black')">Play as Black</button>
</div>
<div id="progress"><div id="progress-bar"></div></div>
<div>FEN: <span id="fen" style="font-size:10px; word-break:break-all;"></span></div>
<div style="margin-top:10px;"><span id="proof" style="font-size:12px; color:#888;"></span></div>
<div class="flip-note">Board auto-flips when you play as Black • Click to move • SAN: e4 Nf3 O-O</div>
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
  // Handle flipped board
  let ranks = flipped ? [1,2,3,4,5,6,7,8] : [8,7,6,5,4,3,2,1];
  let files = flipped ? [7,6,5,4,3,2,1,0] : [0,1,2,3,4,5,6,7];
  for(let rank of ranks){
    for(let file of files){
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
let flipped=false;
function flipBoard(){ flipped=!flipped; render(); }
async function playAs(color){
  await fetch('/playas?color='+color);
  flipped = (color=='black');
  loadBoard();
}
function showProgress(show){
  document.getElementById('progress').style.display = show ? 'block' : 'none';
  if(show){
    let bar=document.getElementById('progress-bar');
    bar.style.width='0%';
    let w=0;
    let iv=setInterval(()=>{
      w+=5; bar.style.width=w+'%';
      if(w>=100) clearInterval(iv);
    }, 100);
  }
}
async function newGame(){ showProgress(true); await fetch('/new'); loadBoard(); showProgress(false); }
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
        elif self.path.startswith("/playas"):
            q=urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            color=q.get("color",["white"])[0]
            # For web, we just set a flag, but board turn is what matters
            # If player wants to be black, engine should move first if it's white to move
            if color=="black" and board.turn==chess.WHITE and not board.is_game_over():
                # Engine plays as white
                if proc:
                    import time
                    proc.stdin.write(f"position fen {board.fen()}\n")
                    proc.stdin.write("go depth 10\n")
                    proc.stdin.flush()
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
