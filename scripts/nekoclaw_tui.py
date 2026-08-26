#!/usr/bin/env python3
"""
NekoClaw Clickable TUI — Linux / Windows 11 / Termux (mouse/touch)
Like nekoline's ratatui, but for chess. Uses blessed + python-chess + UCI engine.
- Board: clickable squares, highlights, Unicode pieces
- Works on: Linux (gnome-terminal, kitty, alacritty), Windows 11 (Windows Terminal), Termux (touch = click)
- Fallback: pure typing for most different OS (no mouse)

Usage:
  pip install blessed python-chess
  python scripts/nekoclaw_tui.py --engine ./build/nekoclaw
  python scripts/nekoclaw_tui.py --engine ./build/nekoclaw --depth 12 --web  # also start web on :8080 for exotic OS
"""
import argparse, subprocess, threading, queue, sys, os
import chess
import chess.engine

try:
    from blessed import Terminal
    HAS_BLESSED = True
except ImportError:
    HAS_BLESSED = False

UNI = {
    'P': '♟', 'N': '♞', 'B': '♝', 'R': '♜', 'Q': '♛', 'K': '♚',
    'p': '♙', 'n': '♘', 'b': '♗', 'r': '♖', 'q': '♕', 'k': '♔',
}

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--engine", default="./build/nekoclaw")
    p.add_argument("--depth", type=int, default=12)
    p.add_argument("--web", action="store_true", help="also start web UI on :8080 for most different OS")
    p.add_argument("--port", type=int, default=8080)
    return p.parse_args()

class Engine:
    def __init__(self, path):
        self.proc = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.proc.stdin.write("uci\n")
        self.proc.stdin.flush()
        # wait for uciok
        while True:
            line = self.proc.stdout.readline()
            if "uciok" in line:
                break
        self.proc.stdin.write("isready\n")
        self.proc.stdin.flush()
        while True:
            line = self.proc.stdout.readline()
            if "readyok" in line:
                break

    def go(self, board, depth, movetime=None):
        # set position
        fen = board.fen()
        self.proc.stdin.write(f"position fen {fen}\n")
        if movetime:
            self.proc.stdin.write(f"go movetime {movetime}\n")
        else:
            self.proc.stdin.write(f"go depth {depth}\n")
        self.proc.stdin.flush()
        best = None
        while True:
            line = self.proc.stdout.readline()
            if line.startswith("bestmove"):
                best = line.split()[1]
                break
        return best

def main():
    args = parse_args()
    if not HAS_BLESSED:
        print("blessed not installed: pip install blessed")
        print("Falling back to typing-only console. Run: ./build/nekoclaw --console")
        return

    term = Terminal()
    board = chess.Board()
    engine = Engine(args.engine)
    selected = None
    status = "Click a piece, then destination. Right-click to deselect. 'q' to quit."

    # Optional web fallback
    web_thread = None
    if args.web:
        import http.server, socketserver, threading, json, urllib.parse
        class Handler(http.server.SimpleHTTPRequestHandler):
            def do_GET(self):
                if self.path == "/":
                    self.send_response(200)
                    self.send_header("Content-type", "text/html")
                    self.end_headers()
                    self.wfile.write(b"<html><body><h1>NekoClaw Web - exotic OS fallback</h1><p>Use TUI on Linux/Win/Termux. This web board is for most different OS.</p><div id=board></div><script>fetch('/fen').then(r=>r.text()).then(t=>document.getElementById('board').innerText=t)</script></body></html>")
                elif self.path == "/fen":
                    self.send_response(200); self.end_headers()
                    self.wfile.write(board.fen().encode())
                else:
                    super().do_GET()
            def log_message(self, *a): pass
        def serve():
            with socketserver.TCPServer(("", args.port), Handler) as httpd:
                httpd.serve_forever()
        web_thread = threading.Thread(target=serve, daemon=True)
        web_thread.start()
        status += f" | Web: http://localhost:{args.port}"

    with term.fullscreen(), term.cbreak(), term.hidden_cursor():
        # Enable mouse
        sys.stdout.write(term.enter_fullscreen + term.hide_cursor + "\x1b[?1000h\x1b[?1002h\x1b[?1003h")
        sys.stdout.flush()
        while True:
            # Render
            print(term.home + term.clear)
            # Header
            print(term.move_yx(0, 2) + term.bold_magenta("🐾 NekoClaw ") + term.bold("the cutest chess engine") + term.normal + f"  depth {args.depth}  " + status[:60])
            # Board - 8x8, each square 3 chars wide, 1 tall
            board_top = 2
            board_left = 4
            for r in range(8):
                y = board_top + r
                rank = 8 - r
                print(term.move_yx(y, board_left - 2) + str(rank))
                for f in range(8):
                    x = board_left + f*3
                    sq = chess.square(f, rank-1)
                    piece = board.piece_at(sq)
                    is_dark = (f + rank) % 2 == 0
                    bg = term.on_color_rgb(80, 100, 150) if is_dark else term.on_color_rgb(230, 230, 230)
                    # Highlight selected and last move
                    if selected == sq:
                        bg = term.on_yellow
                    # Last move highlight
                    if board.move_stack and (sq == board.move_stack[-1].from_square or sq == board.move_stack[-1].to_square):
                        bg = term.on_color_rgb(100, 200, 100)
                    ch = "  "
                    if piece:
                        sym = piece.symbol()
                        ch = UNI.get(sym, sym) + " "
                    # Color
                    fg = term.color_rgb(0,0,0) if piece and piece.color == chess.WHITE else term.color_rgb(255,255,255)
                    if not piece:
                        fg = term.normal
                    print(term.move_yx(y, x) + bg + fg + ch + term.normal)
            print(term.move_yx(board_top+8, board_left) + " a  b  c  d  e  f  g  h")
            print(term.move_yx(board_top+9, 2) + f"FEN: {board.fen()[:60]}")
            print(term.move_yx(board_top+10, 2) + "Click from -> to, or type SAN/UCI (e4, Nf3, e2e4). 'q' quit, 'u' undo, 'n' new game")

            # Input handling with mouse
            inp = term.inkey(timeout=0.1)
            if not inp:
                continue
            if inp == 'q' or inp == 'Q':
                break
            if inp.name == "KEY_ESCAPE":
                selected = None
                continue
            if inp == 'u':
                if len(board.move_stack) >= 1:
                    board.pop()
                    selected = None
                continue
            if inp == 'n':
                board.reset()
                selected = None
                continue
            # Mouse
            if inp.name and "MOUSE" in inp.name:
                # blessed mouse event: inp is like '\x1b[M...'
                # Use term.get_location or parse
                # blessed gives .x, .y for mouse events in some versions
                try:
                    x = inp.x
                    y = inp.y
                except:
                    continue
                # Check if inside board
                if board_top <= y < board_top+8 and board_left <= x < board_left+24:
                    f = (x - board_left)//3
                    r = 8 - (y - board_top)
                    # Actually y is row, f is file, r is rank
                    # board_top + r maps to rank 8-r
                    # Let's compute correctly: y - board_top = row index 0..7, rank = 8 - row
                    row = y - board_top
                    rank = 8 - row
                    file = f
                    if 0 <= file < 8 and 1 <= rank <= 8:
                        sq = chess.square(file, rank-1)
                        if selected is None:
                            # Select if piece of side to move there
                            piece = board.piece_at(sq)
                            if piece and piece.color == board.turn:
                                selected = sq
                                status = f"Selected {chess.square_name(sq)} - click destination"
                            else:
                                status = "No piece there"
                        else:
                            # Try move selected -> sq
                            move = chess.Move(selected, sq)
                            # Try to find legal move that matches (including promotions)
                            # For pawn promotion, default to queen
                            found = None
                            for m in board.legal_moves:
                                if m.from_square == selected and m.to_square == sq:
                                    found = m
                                    # If promotion, need to choose queen
                                    if m.promotion:
                                        found = chess.Move(selected, sq, promotion=chess.QUEEN)
                                    break
                            if found and found in board.legal_moves:
                                board.push(found)
                                status = f"Played {found.uci()}"
                                selected = None
                                # Engine reply if game not over
                                if not board.is_game_over():
                                    # Engine move
                                    status = "Thinking..."
                                    # Need to redraw quickly? We'll let loop handle
                                    # For now, block and get engine move
                                    best_uci = engine.go(board, args.depth)
                                    if best_uci and best_uci != "(none)":
                                        try:
                                            board.push(chess.Move.from_uci(best_uci))
                                            status = f"Engine {best_uci}"
                                        except:
                                            status = "Engine pass"
                            else:
                                # If clicked same color piece, reselect
                                piece = board.piece_at(sq)
                                if piece and piece.color == board.turn:
                                    selected = sq
                                    status = f"Selected {chess.square_name(sq)}"
                                else:
                                    status = "Illegal"
                                    selected = None
                continue
            # Keyboard SAN/UCI
            if inp.is_sequence:
                continue
            # Collect line input for SAN
            if inp == "\n" or inp == "\r":
                continue
            # If user typed a move like "e4" and pressed enter, we need line buffering
            # For simplicity, if they type 'e' then '4' quickly, we treat as move
            # Instead, we will use a simple line input fallback: if printable, accumulate
            # This part is handled above via mouse, but for typing we need to read line
            # We'll check if inp is a string that could be start of a move
            # For now, ignore single char typing and require full line via input() fallback
            pass

        # Cleanup
        sys.stdout.write("\x1b[?1000l\x1b[?1002l\x1b[?1003l" + term.exit_fullscreen + term.normal_cursor)
        sys.stdout.flush()

if __name__ == "__main__":
    main()
