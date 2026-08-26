//! NekoClaw TUI — the cutest chess engine in the world 🐾
//! Gorgeous lichess-styled board, fully async UCI engine, SAN + mouse, cat
//! CC0 reimagined pieces (cburnett inspired), GPL-3.0 Vaibhav

use crossterm::event::{self, Event, KeyCode, MouseEventKind};
use ratatui::{prelude::*, widgets::*};
use shakmaty::{Chess, Position, Square, Role, Move, Color as ChessColor, EnPassantMode, fen::Fen, san::San, uci::UciMove, File, Rank};
use std::io::{self, Write, BufRead};
use std::process::{Command, Stdio, Child, ChildStdin, ChildStdout};
use std::sync::mpsc::{self, Sender, Receiver};
use std::thread;
use std::time::{Duration, Instant};
use std::path::Path;

// ── Lichess palette — cburnett inspired ──
const LIGHT: Color = Color::Rgb(240, 217, 181); // #F0D9B5
const DARK: Color = Color::Rgb(181, 136, 99);   // #B58863
const LIGHT_HL: Color = Color::Rgb(205, 210, 106); // #CDD26A highlight light
const DARK_HL: Color = Color::Rgb(170, 162, 58);   // #AAA23B highlight dark
const SELECT_BG: Color = Color::Rgb(246, 246, 105); // #F6F669 selected
const SELECT_FG: Color = Color::Rgb(0, 0, 0);
const CHECK_BG: Color = Color::Rgb(255, 82, 82);
const COORD: Color = Color::Rgb(255, 215, 0);
const HEADER_BG: Color = Color::Rgb(25, 25, 35);
const BORDER_PINK: Color = Color::Rgb(255, 105, 180);
const BORDER_PURPLE: Color = Color::Rgb(180, 80, 255);
const BORDER_BLUE: Color = Color::Rgb(100, 180, 255);
const BORDER_GREEN: Color = Color::Rgb(0, 255, 136);
const CAT_FG: Color = Color::Rgb(255, 180, 220);
const WHITE_PIECE: Color = Color::Rgb(255, 255, 255);
const BLACK_PIECE: Color = Color::Rgb(0, 0, 0);
const SHADOW_WHITE_ON_LIGHT: Color = Color::Rgb(80, 40, 20); // outline hint
const HALO_BLACK_ON_DARK: Color = Color::Rgb(255, 248, 220); // halo for black on dark

fn piece_glyph(board: &shakmaty::Board, sq: Square) -> (&'static str, bool) {
    match board.piece_at(sq) {
        None => (" ", false),
        Some(p) => match (p.color, p.role) {
            (ChessColor::White, Role::Pawn) => ("♟", true),
            (ChessColor::White, Role::Knight) => ("♞", true),
            (ChessColor::White, Role::Bishop) => ("♝", true),
            (ChessColor::White, Role::Rook) => ("♜", true),
            (ChessColor::White, Role::Queen) => ("♛", true),
            (ChessColor::White, Role::King) => ("♚", true),
            (ChessColor::Black, Role::Pawn) => ("♙", false),
            (ChessColor::Black, Role::Knight) => ("♘", false),
            (ChessColor::Black, Role::Bishop) => ("♗", false),
            (ChessColor::Black, Role::Rook) => ("♖", false),
            (ChessColor::Black, Role::Queen) => ("♕", false),
            (ChessColor::Black, Role::King) => ("♔", false),
        },
    }
}

// ── Cat ──
const CAT_FRAMES: &[&str] = &[
    "=^._.^=",
    "=^･ω･^=",
    "=^..^=",
    "=^◕ᴥ◕^=",
];
const CAT_STR_W: usize = 8; // approximate cell width usage

#[derive(Debug)]
enum EngineCmd {
    Go { fen: String, depth: u32 },
    Stop,
    Quit,
}

#[derive(Debug)]
enum EngineResp {
    BestMove { uci: String, score: String, depth: u32, nodes: u64 },
    Info { depth: u32, score: String, nodes: u64, nps: u64, pv: String },
    Error(String),
}

struct EngineHandle {
    tx: Sender<EngineCmd>,
    rx: Receiver<EngineResp>,
}

fn parse_info(line: &str) -> Option<(u32, String, u64, u64, String)> {
    // info depth 8 score cp 10 nodes 1199237 nps 3579811 ... pv f2f3 ...
    if !line.starts_with("info") { return None; }
    let toks: Vec<&str> = line.split_whitespace().collect();
    let mut depth: Option<u32> = None;
    let mut score: Option<String> = None;
    let mut nodes: Option<u64> = None;
    let mut nps: Option<u64> = None;
    let mut pv_start: Option<usize> = None;
    let mut i = 0;
    while i < toks.len() {
        match toks[i] {
            "depth" if i + 1 < toks.len() => { depth = toks[i+1].parse().ok(); i+=2; continue; }
            "score" if i+2 < toks.len() && toks[i+1]=="cp" => {
                if let Ok(cp) = toks[i+2].parse::<i32>() {
                    score = Some(format!("{:+.2}", cp as f32/100.0));
                }
                i+=3; continue;
            }
            "score" if i+2 < toks.len() && toks[i+1]=="mate" => {
                score = Some(format!("M{}", toks[i+2]));
                i+=3; continue;
            }
            "nodes" if i+1 < toks.len() => { nodes = toks[i+1].parse().ok(); i+=2; continue; }
            "nps" if i+1 < toks.len() => { nps = toks[i+1].parse().ok(); i+=2; continue; }
            "pv" => { pv_start = Some(i+1); break; }
            _ => {}
        }
        i+=1;
    }
    let pv = if let Some(s)=pv_start { toks[s..].join(" ") } else { String::new() };
    Some((depth.unwrap_or(0), score.unwrap_or("0.00".into()), nodes.unwrap_or(0), nps.unwrap_or(0), pv))
}

fn find_engine_path() -> Option<String> {
    let candidates = [
        "./build/nekoclaw",
        "build/nekoclaw",
        "../build/nekoclaw",
        "/home/neko/nekoclaw/build/nekoclaw",
        "./nekoclaw",
        "/usr/local/bin/nekoclaw",
    ];
    for p in candidates {
        if Path::new(p).exists() { return Some(p.to_string()); }
    }
    // try relative to tui crate root: ../build
    if let Ok(cur) = std::env::current_dir() {
        let try_path = cur.join("../build/nekoclaw");
        if try_path.exists() { return Some(try_path.to_string_lossy().into_owned()); }
        let try_path2 = cur.join("../../build/nekoclaw");
        if try_path2.exists() { return Some(try_path2.to_string_lossy().into_owned()); }
    }
    None
}

fn spawn_engine() -> Option<EngineHandle> {
    let path = find_engine_path()?;
    let mut child = Command::new(&path).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null()).spawn().ok()?;
    let stdin = child.stdin.take()?;
    let stdout = child.stdout.take()?;
    let (cmd_tx, cmd_rx) = mpsc::channel::<EngineCmd>();
    let (resp_tx, resp_rx) = mpsc::channel::<EngineResp>();
    thread::spawn(move || engine_thread(stdin, stdout, cmd_rx, resp_tx, child));
    Some(EngineHandle { tx: cmd_tx, rx: resp_rx })
}

fn engine_thread(mut stdin: ChildStdin, stdout: ChildStdout, cmd_rx: Receiver<EngineCmd>, resp_tx: Sender<EngineResp>, mut _child: Child) {
    let reader = io::BufReader::new(stdout);
    let (line_tx, line_rx) = mpsc::channel::<String>();
    thread::spawn(move || {
        for line in reader.lines() {
            match line {
                Ok(l) => { if line_tx.send(l).is_err() { break; } },
                Err(_) => break,
            }
        }
    });
    // handshake: uci
    let _ = stdin.write_all(b"uci\n");
    let _ = stdin.flush();
    let start = Instant::now();
    let mut got_uciok = false;
    while start.elapsed() < Duration::from_secs(3) {
        if let Ok(line) = line_rx.try_recv() {
            if line.trim() == "uciok" { got_uciok = true; break; }
            if line.contains("uciok") { got_uciok = true; break; }
        } else {
            thread::sleep(Duration::from_millis(5));
        }
    }
    // optional log; continue anyway
    let _ = stdin.write_all(b"isready\n");
    let _ = stdin.flush();
    let start = Instant::now();
    while start.elapsed() < Duration::from_secs(2) {
        if let Ok(line) = line_rx.try_recv() {
            if line.contains("readyok") { break; }
        } else {
            thread::sleep(Duration::from_millis(5));
        }
    }
    while let Ok(_) = line_rx.try_recv() {} // drain
    let _ = got_uciok; // silence
    loop {
        let cmd = match cmd_rx.recv() {
            Ok(c) => c,
            Err(_) => break,
        };
        match cmd {
            EngineCmd::Go { fen, depth } => {
                let _ = stdin.write_all(format!("position fen {}\n", fen).as_bytes());
                let _ = stdin.write_all(format!("go depth {}\n", depth).as_bytes());
                let _ = stdin.flush();
                let start = Instant::now();
                let mut best: Option<String> = None;
                let mut last_score = "0.00".to_string();
                let mut last_depth = 0u32;
                let mut last_nodes = 0u64;
                // collect until bestmove or timeout
                while start.elapsed() < Duration::from_secs(12) {
                    if let Ok(line) = line_rx.try_recv() {
                        if line.starts_with("bestmove") {
                            let parts: Vec<&str> = line.split_whitespace().collect();
                            if parts.len() >= 2 && parts[1] != "(none)" {
                                best = Some(parts[1].to_string());
                            }
                            break;
                        } else if line.starts_with("info") {
                            if let Some((d, s, n, nps, pv)) = parse_info(&line) {
                                if d != 0 { last_depth = d; }
                                if s != "0.00" { last_score = s.clone(); }
                                if n != 0 { last_nodes = n; }
                                let _ = resp_tx.send(EngineResp::Info { depth: d, score: s, nodes: n, nps, pv });
                            }
                        }
                    } else {
                        thread::sleep(Duration::from_millis(4));
                    }
                    // check if a new Go arrived? The outer recv will block until this go finishes, so ignore
                }
                if let Some(uci) = best {
                    let _ = resp_tx.send(EngineResp::BestMove { uci, score: last_score, depth: last_depth, nodes: last_nodes });
                } else {
                    let _ = resp_tx.send(EngineResp::Error("no bestmove".into()));
                }
            },
            EngineCmd::Stop => {
                let _ = stdin.write_all(b"stop\n");
                let _ = stdin.flush();
            },
            EngineCmd::Quit => {
                let _ = stdin.write_all(b"quit\n");
                let _ = stdin.flush();
                break;
            },
        }
    }
    // best effort kill
    let _ = _child.kill();
}

fn try_san(board: &Chess, input: &str) -> Option<Move> {
    let s = input.trim();
    if s.is_empty() { return None; }
    // Normalize castling: allow 0-0, 0-0-0
    let mut norm = s.to_string();
    if norm == "0-0" { norm = "O-O".into(); }
    if norm == "0-0-0" { norm = "O-O-O".into(); }
    // Try SAN first
    if let Ok(san) = San::from_ascii(norm.as_bytes()) {
        if let Ok(mv) = san.to_move(board) {
            if board.is_legal(mv.clone()) { return Some(mv); }
        }
    }
    // Try SAN with + # stripped? Shakmaty San already handles but ensure
    let stripped = norm.trim_end_matches(|c| c=='+' || c=='#' || c=='!' || c=='?');
    if stripped != norm {
        if let Ok(san) = San::from_ascii(stripped.as_bytes()) {
            if let Ok(mv) = san.to_move(board) {
                if board.is_legal(mv.clone()) { return Some(mv); }
            }
        }
    }
    // Try UCI
    if let Ok(uci) = s.parse::<UciMove>() {
        if let Ok(mv) = uci.to_move(board) {
            if board.is_legal(mv.clone()) { return Some(mv); }
        }
    }
    let lower = s.to_ascii_lowercase();
    if lower != s {
        if let Ok(uci) = lower.parse::<UciMove>() {
            if let Ok(mv) = uci.to_move(board) {
                if board.is_legal(mv.clone()) { return Some(mv); }
            }
        }
    }
    None
}

fn san_of(board: &Chess, mv: Move) -> String {
    // generate SAN from move
    let san = San::from_move(board, mv.clone());
    san.to_string()
}

#[derive(Clone, Copy)]
struct BoardGeom {
    board_area: Rect,
    cell_w: u16,
    cell_h: u16,
    board_w: u16,
    board_h: u16,
    left: u16,
    top: u16,
}

fn compute_geom(total: Rect) -> BoardGeom {
    // inner = total with outer border subtracted (1 each side)
    let outer = Rect { x: total.x+1, y: total.y+1, width: total.width.saturating_sub(2), height: total.height.saturating_sub(2) };
    // header 1 row
    let board_container = Rect { x: outer.x, y: outer.y+1, width: outer.width, height: outer.height.saturating_sub(1) };
    // split 80% board, 20% stats — fill 80% terminal as spec
    let chunks = Layout::default().direction(Direction::Horizontal).constraints([Constraint::Percentage(80), Constraint::Percentage(20)]).split(board_container);
    let board_area = chunks[0];
    // cell size: at least 7x3 or 9x4, auto-scale
    let cell_w = ((board_area.width.saturating_sub(6) / 8).max(7).min(13)) as u16;
    let cell_h = ((board_area.height.saturating_sub(6) / 8).max(3).min(6)) as u16;
    let board_w = cell_w * 8;
    let board_h = cell_h * 8;
    let left = board_area.x + board_area.width.saturating_sub(board_w)/2;
    let top = board_area.y + 1 + board_area.height.saturating_sub(board_h+2)/2;
    BoardGeom { board_area, cell_w, cell_h, board_w, board_h, left, top }
}

fn main() -> io::Result<()> {
    let mut board = Chess::default();
    let mut history: Vec<Chess> = Vec::new();
    let mut selected: Option<Square> = None;
    let mut flipped = false;
    let mut player_color = ChessColor::White;
    let mut show_help = false;
    let mut last_move: Option<Move> = None;
    let mut status = String::from("🐾 Click piece → destination  •  Type SAN: e4 Nf3 O-O  Enter  •  q quit  n new  u undo");
    let mut input_buf = String::new();
    let mut last_eval = "0.00".to_string();
    let mut last_depth: u32 = 0;
    let mut last_nodes: u64 = 0;
    let mut last_nps: u64 = 3400000;
    let mut engine_thinking = false;
    let mut legal_dests: Vec<Square> = Vec::new();

    // cat
    let mut cat_pos: usize = 0;
    let mut cat_frame: usize = 0;
    let cat_dir: i32 = 1;
    let mut last_cat = Instant::now();
    let mut cat_pause = 0usize;

    // engine spawn
    let engine = spawn_engine();
    let engine_available = engine.is_some();
    if engine.is_none() {
        status = "⚠️ Engine not found (play hotseat 2P) • Click or SAN to move  •  q quit".into();
    }
    // hold engine handle mutable through loop
    let engine_handle = engine;

    crossterm::terminal::enable_raw_mode()?;
    crossterm::execute!(io::stdout(), crossterm::terminal::EnterAlternateScreen, crossterm::event::EnableMouseCapture)?;
    let backend = CrosstermBackend::new(io::stdout());
    let mut terminal = Terminal::new(backend)?;
    // hide cursor
    let mut last_tick = Instant::now();

    // For nps display smoothing
    let mut last_info_pv = String::new();

    'outer: loop {
        // Poll engine responses non-blocking each frame before draw so board updates instantly
        if let Some(ref eng) = engine_handle {
            while let Ok(resp) = eng.rx.try_recv() {
                match resp {
                    EngineResp::Info { depth, score, nodes, nps, pv } => {
                        last_depth = depth;
                        last_eval = score.clone();
                        last_nodes = nodes;
                        if nps != 0 { last_nps = nps; }
                        last_info_pv = pv;
                        // don't stop thinking yet
                    },
                    EngineResp::BestMove { uci, score, depth, nodes } => {
                        engine_thinking = false;
                        if !uci.is_empty() {
                            // Convert UCI to Move using current board
                            if let Ok(um) = uci.parse::<UciMove>() {
                                if let Ok(mv) = um.to_move(&board) {
                                    if board.is_legal(mv.clone()) {
                                        // generate SAN before play for status
                                        let san_str = san_of(&board, mv.clone());
                                        history.push(board.clone());
                                        board = board.play(mv.clone()).unwrap();
                                        last_move = Some(mv.clone());
                                        selected = None;
                                        legal_dests.clear();
                                        if !score.is_empty() { last_eval = score; }
                                        if depth !=0 { last_depth = depth; }
                                        if nodes !=0 { last_nodes = nodes; }
                                        status = format!("😺 Neko plays {} ({})", san_str, uci);
                                        if board.is_check() { status.push_str("  ☠️ CHECK!"); }
                                        if board.is_game_over() { status = format!("Game over: {:?}", board.outcome()); }
                                    } else {
                                        status = format!("Engine illegal: {}", uci);
                                    }
                                } else {
                                    status = format!("Engine bad move: {}", uci);
                                }
                            }
                        } else {
                            status = "Engine: no move".into();
                        }
                    },
                    EngineResp::Error(e) => {
                        engine_thinking = false;
                        status = format!("Engine: {}", e);
                    },
                }
            }
        }

        // Cat animation
        if last_cat.elapsed() > Duration::from_millis(320) {
            last_cat = Instant::now();
            if cat_pause > 0 {
                cat_pause -= 1;
            } else {
                let max_pos = 28; // will be clamped to board_w later
                let _ = max_pos;
                cat_pos = (cat_pos as i32 + cat_dir) as usize;
                cat_frame = (cat_frame + 1) % CAT_FRAMES.len();
                // bounce at edges: rely on board_w during draw clamping
            }
            // occasionally pause and wink
            if cat_pos % 13 == 0 && cat_pause == 0 && rand_tick() {
                cat_pause = 2;
                cat_frame = 3;
            }
        }

        // Compute geom for draw & for later input handling? We'll compute inside draw but also need for next iteration mouse.
        // Do draw
        {
            let cat_x_for_status = cat_pos;
            let cat_current = CAT_FRAMES[cat_frame];
            // we need to share history etc immutably inside closure
            let input_display = input_buf.clone();
            let status_clone = status.clone();
            let board_clone = board.clone();
            let selected_clone = selected.clone();
            let last_move_clone = last_move.clone();
            let legal_dests_clone = legal_dests.clone();
            let last_eval_clone = last_eval.clone();
            let last_depth_clone = last_depth;
            let last_nodes_clone = last_nodes;
            let last_nps_clone = last_nps;
            let last_pv_clone = last_info_pv.clone();
            let thinking_clone = engine_thinking;
            let avail_clone = engine_available;
            let history_len = history.len();

            terminal.draw(|f| {
                let area = f.area();
                let outer = Block::default()
                    .borders(Borders::ALL)
                    .title("  🐾 NekoClaw  v1.0.0  —  Vaibhav  —  the cutest chess engine in the world  ♔  ")
                    .title_style(Style::default().fg(BORDER_PINK).add_modifier(Modifier::BOLD))
                    .border_type(BorderType::Rounded)
                    .border_style(Style::default().fg(BORDER_PURPLE).add_modifier(Modifier::BOLD));
                f.render_widget(outer, area);
                let inner = Rect { x: area.x+1, y: area.y+1, width: area.width.saturating_sub(2), height: area.height.saturating_sub(2) };

                // Header
                let header_style = Style::default().bg(HEADER_BG).fg(Color::Rgb(100, 255, 180)).add_modifier(Modifier::BOLD);
                let header_text = if !input_display.is_empty() {
                    format!("⌨ SAN: {}█  (Enter to play, Esc to clear)  •  {}", input_display, status_clone)
                } else {
                    status_clone.clone()
                };
                let header = Paragraph::new(header_text)
                    .style(header_style)
                    .alignment(Alignment::Center)
                    .wrap(Wrap { trim: true });
                f.render_widget(header, Rect { x: inner.x, y: inner.y, width: inner.width, height: 1 });

                // Layout: board 74% + stats 26%
                let chunks = Layout::default()
                    .direction(Direction::Horizontal)
                    .constraints([Constraint::Percentage(80), Constraint::Percentage(20)])
                    .split(Rect { x: inner.x, y: inner.y+1, width: inner.width, height: inner.height.saturating_sub(1) });
                let board_area = chunks[0];
                let stats_area = chunks[1];

                // Compute geometry matching main compute_geom
                let cell_w = (board_area.width.saturating_sub(6) / 8).max(7).min(13);
                let cell_h = (board_area.height.saturating_sub(6) / 8).max(3).min(6);
                let board_w = cell_w * 8;
                let board_h = cell_h * 8;
                let board_left = board_area.x + board_area.width.saturating_sub(board_w)/2;
                let board_top = board_area.y + 1 + board_area.height.saturating_sub(board_h+2)/2;

                // Board outer with cat on top edge
                let board_block = Block::default()
                    .borders(Borders::ALL)
                    .title("  ♟ Board — click to move  ")
                    .title_style(Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD))
                    .border_type(BorderType::Rounded)
                    .border_style(Style::default().fg(BORDER_BLUE).add_modifier(Modifier::BOLD));
                f.render_widget(board_block, Rect { x: board_left.saturating_sub(1), y: board_top.saturating_sub(1), width: board_w+2, height: board_h+2 });

                // Cat walks along the top border (just above squares but on the border line)
                // Position cat along board_w; ensure it stays within border
                let cat_len = cat_current.chars().count() as u16;
                let max_cat_x = board_w.saturating_sub(cat_len + 1);
                let cat_x = if max_cat_x == 0 { 0 } else { (cat_x_for_status % (max_cat_x as usize + 1)) as u16 };
                // cat y is board_top -1 (the border top)
                let cat_y = board_top.saturating_sub(1);
                let cat_style = Style::default().fg(CAT_FG).bg(Color::Rgb(25, 25, 35)).add_modifier(Modifier::BOLD);
                // small paws trail behind
                let trail = if cat_frame % 2 == 0 { "· ·" } else { "  ·" };
                if max_cat_x > 10 {
                    f.render_widget(Paragraph::new(cat_current).style(cat_style), Rect { x: board_left + cat_x, y: cat_y, width: cat_len, height: 1 });
                    // tiny tail wobble dots behind cat
                    let tx = board_left + cat_x.saturating_sub(3);
                    if cat_x >= 3 {
                        f.render_widget(Paragraph::new(trail).style(Style::default().fg(Color::Rgb(255,150,200))), Rect { x: tx, y: cat_y, width: 3, height: 1 });
                    }
                }

                // Files top
                let mut top = String::new();
                for file in 0..8 {
                    top.push_str(&format!("{:^w$}", (b'a'+file) as char, w=cell_w as usize));
                }
                // render above board but below border? Use board_top-1 row currently occupied by cat, so shift files to top border title area?
                // Instead render inside board top border as overlay? Simpler: render on y = board_top-1 offset? We'll render just below cat if overlap.
                // Actually already have cat on border, files are drawn on same row - shift files down by using style over border will overwrite cat? So draw files at y=board_top (inside) top coord? We'll draw files at board_top-1 but with gaps to not overwrite cat - so skip.
                // Let's draw files at y = board_top + board_h (bottom) and top as subtle inside row
                // For now draw top files in a slightly dimmer row just above board inside
                // We will draw top coordinates at y = board_top (first rank row) as header? Simpler keep as before but avoid cat overlap by moving top label to board_top-1 but cat already there. So draw top files at board_top inside with offset?
                // Draw top files as faint inside first row background? Approach: draw at board_top -1 but cat will overwrite part, still okay cat walks over letters - cute!
                f.render_widget(Paragraph::new(top.clone()).style(Style::default().fg(COORD).add_modifier(Modifier::BOLD)), Rect { x: board_left, y: board_top.saturating_sub(1), width: board_w, height: 1 });

                // Determine last move squares for highlight
                let (lm_from, lm_to) = match last_move_clone {
                    Some(m) => (m.from(), Some(m.to())),
                    None => (None, None),
                };
                let check_king = if board_clone.is_check() { board_clone.board().king_of(board_clone.turn()) } else { None };

                for r in 0..8 {
                    let rank = if flipped { r+1 } else { 8 - r };
                    let y0 = board_top + r as u16 * cell_h;
                    // Rank left
                    f.render_widget(Paragraph::new(format!("{:>2}", rank)).style(Style::default().fg(COORD).add_modifier(Modifier::BOLD)), Rect { x: board_left.saturating_sub(3), y: y0 + cell_h/2, width: 2, height: 1 });
                    for file in 0..8 {
                        let disp_file = if flipped { 7 - file } else { file };
                        let x0 = board_left + file as u16 * cell_w;
                        let sq = Square::from_coords(File::try_from(disp_file as u8).unwrap(), Rank::try_from((rank-1) as u8).unwrap());
                        let is_light = (file as u8 + (rank as u8)) % 2 == 0; // h1 light (7+1=8 even)
                        let mut bg = if is_light { LIGHT } else { DARK };
                        let mut is_hl = false;
                        // last move highlight
                        if Some(sq) == lm_from || Some(sq) == lm_to {
                            bg = if is_light { LIGHT_HL } else { DARK_HL };
                            is_hl = true;
                        }
                        let is_selected = Some(sq) == selected_clone;
                        let is_check_sq = Some(sq) == check_king;
                        // base style
                        let base = if is_selected {
                            Style::default().bg(SELECT_BG).fg(SELECT_FG).add_modifier(Modifier::BOLD)
                        } else if is_check_sq {
                            Style::default().bg(CHECK_BG).fg(Color::White).add_modifier(Modifier::BOLD | Modifier::SLOW_BLINK)
                        } else if is_hl {
                            Style::default().bg(bg).fg(Color::Black)
                        } else {
                            Style::default().bg(bg)
                        };

                        // check if this square is legal dest for selected piece
                        let is_legal_dest = legal_dests_clone.contains(&sq) && board_clone.board().piece_at(sq).is_none();
                        let is_legal_capture = legal_dests_clone.contains(&sq) && board_clone.board().piece_at(sq).is_some();

                        for dy in 0..cell_h {
                            let (glyph, is_white) = piece_glyph(board_clone.board(), sq);
                            let is_piece_row = dy == cell_h / 2;
                            let is_hint_row = dy == cell_h / 2;
                            let content = if is_piece_row {
                                if glyph.trim().is_empty() {
                                    if is_legal_dest {
                                        // dot for move hint
                                        let dot = "·";
                                        format!("{:^w$}", dot, w=cell_w as usize)
                                    } else {
                                        " ".repeat(cell_w as usize)
                                    }
                                } else {
                                    // piece centered with padding
                                    let inner = format!(" {} ", glyph);
                                    format!("{:^w$}", inner, w=cell_w as usize)
                                }
                            } else if is_hint_row && glyph.trim().is_empty() && is_legal_dest {
                                // already handled? but keep for double ensure
                                " ".repeat(cell_w as usize)
                            } else {
                                " ".repeat(cell_w as usize)
                            };
                            let mut style = base;
                            if is_piece_row && !glyph.trim().is_empty() {
                                // capture tint for enemy pieces that are legal captures
                                if is_legal_capture && !is_selected && !is_check_sq && !is_hl {
                                    let cap_bg = if is_light { Color::Rgb(240, 180, 170) } else { Color::Rgb(200, 120, 110) };
                                    style = Style::default().bg(cap_bg).fg(if is_white { WHITE_PIECE } else { BLACK_PIECE }).add_modifier(Modifier::BOLD);
                                } else if is_white {
                                    if is_light && !is_selected && !is_check_sq && !is_hl {
                                        let alt_bg = Color::Rgb(220, 195, 160);
                                        style = Style::default().bg(alt_bg).fg(WHITE_PIECE).add_modifier(Modifier::BOLD);
                                        if is_hl { style = Style::default().bg(bg).fg(WHITE_PIECE).add_modifier(Modifier::BOLD); }
                                    } else {
                                        style = style.fg(WHITE_PIECE).add_modifier(Modifier::BOLD);
                                    }
                                } else {
                                    if !is_light && !is_selected && !is_check_sq {
                                        let alt_bg = Color::Rgb(200, 160, 120);
                                        style = Style::default().bg(if is_hl { bg } else { alt_bg }).fg(BLACK_PIECE).add_modifier(Modifier::BOLD);
                                    } else {
                                        style = style.fg(BLACK_PIECE).add_modifier(Modifier::BOLD);
                                    }
                                }
                            } else if is_piece_row && glyph.trim().is_empty() && is_legal_dest {
                                // hint dot style
                                style = if is_light {
                                    Style::default().bg(bg).fg(Color::Rgb(60, 60, 60))
                                } else {
                                    Style::default().bg(bg).fg(Color::Rgb(30, 30, 30))
                                };
                                style = style.add_modifier(Modifier::BOLD);
                            }
                            f.render_widget(Paragraph::new(content).style(style), Rect { x: x0, y: y0 + dy, width: cell_w, height: 1 });
                        }
                    }
                    f.render_widget(Paragraph::new(format!(" {}", rank)).style(Style::default().fg(COORD)), Rect { x: board_left+board_w, y: y0 + cell_h/2, width: 2, height: 1 });
                }
                // Files bottom
                let mut bot = String::new();
                for file in 0..8 { let disp_file = if flipped { 7 - file } else { file }; bot.push_str(&format!("{:^w$}", (b'a'+disp_file as u8) as char, w=cell_w as usize)); }
                f.render_widget(Paragraph::new(bot).style(Style::default().fg(COORD)), Rect { x: board_left, y: board_top+board_h, width: board_w, height: 1 });

                // Stats — super colorful
                let stats_block = Block::default()
                    .borders(Borders::ALL)
                    .title(if thinking_clone { " ⚡ Thinking… " } else { " ⚡ NekoClaw Engine " })
                    .title_style(Style::default().fg(BORDER_GREEN).add_modifier(Modifier::BOLD))
                    .border_type(BorderType::Rounded)
                    .border_style(Style::default().fg(BORDER_GREEN).add_modifier(Modifier::BOLD));
                f.render_widget(stats_block, stats_area);
                let s = Rect { x: stats_area.x+1, y: stats_area.y+1, width: stats_area.width.saturating_sub(2), height: stats_area.height.saturating_sub(2) };
                let fen = Fen::from_position(&board_clone as &Chess, EnPassantMode::Legal).to_string();
                let turn = if board_clone.turn() == ChessColor::White { "White ♔" } else { "Black ♚" };
                let check = if board_clone.is_check() { "  ☠️ CHECK!" } else { "" };
                let outcome = if board_clone.is_game_over() { format!("{:?}", board_clone.outcome()) } else { if thinking_clone { "🤔 Engine thinking…".into() } else { "Playing…".into() } };
                // Truncate FEN to fit width s.width
                let fen_wrap = {
                    let w = s.width as usize;
                    if w == 0 { fen.clone() } else {
                        fen.chars().collect::<Vec<_>>().chunks(w.max(1)).map(|c| c.iter().collect::<String>()).collect::<Vec<_>>().join("\n")
                    }
                };
                let eval_color = if last_eval_clone.starts_with('+') || last_eval_clone.starts_with("M") && !last_eval_clone.contains('-') { Color::Rgb(100, 255, 100) } else if last_eval_clone.starts_with('-') { Color::Rgb(255, 100, 100) } else { Color::White };
                let stats_text = vec![
                    Line::from(vec![Span::styled("● Side: ", Style::default().fg(Color::Cyan).add_modifier(Modifier::BOLD)), Span::styled(format!("{}{}", turn, check), Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD))]),
                    Line::from(""),
                    Line::from(vec![Span::styled("♟ Eval:  ", Style::default().fg(Color::Cyan).add_modifier(Modifier::BOLD)), Span::styled(last_eval_clone.clone(), Style::default().fg(eval_color).add_modifier(Modifier::BOLD))]),
                    Line::from(vec![Span::styled("⛩ Depth: ", Style::default().fg(Color::Cyan)), Span::styled(format!("{}", last_depth_clone), Style::default().fg(Color::White))]),
                    Line::from(vec![Span::styled("⚡ Nodes: ", Style::default().fg(Color::Cyan)), Span::styled(format!("{}", last_nodes_clone), Style::default().fg(Color::White))]),
                    Line::from(vec![Span::styled("⟡ NPS:   ", Style::default().fg(Color::Cyan)), Span::styled(format!("{}", last_nps_clone), Style::default().fg(Color::White))]),
                    Line::from(vec![Span::styled("♦ PV:    ", Style::default().fg(Color::Cyan)), Span::styled(last_pv_clone.clone(), Style::default().fg(Color::Rgb(180, 220, 255)))]),
                    Line::from(""),
                    Line::from(Span::styled("─ Status ─", Style::default().fg(Color::Magenta).add_modifier(Modifier::BOLD))),
                    Line::from(Span::styled(if thinking_clone { "😺 Thinking…".to_string() } else { status_clone.clone() }, Style::default().fg(Color::White))),
                    Line::from(""),
                    Line::from(Span::styled(format!("─ Game ({} ply) ─", history_len), Style::default().fg(Color::Magenta))),
                    Line::from(Span::raw(outcome)),
                    Line::from(""),
                    Line::from(Span::styled("─ FEN ─", Style::default().fg(Color::DarkGray))),
                    Line::from(Span::raw(fen_wrap)),
                    Line::from(""),
                    Line::from(Span::styled("─ Controls ─", Style::default().fg(BORDER_GREEN).add_modifier(Modifier::BOLD))),
                    Line::from(Span::raw("Click → Click  •  e4  Nf3  O-O  Enter")),
                    Line::from(Span::raw("q quit  •  n new  •  u undo  •  Esc clear")),
                    Line::from(Span::styled(if avail_clone { "Engine: ready ✨" } else { "Engine: offline (hotseat)" }, Style::default().fg(if avail_clone { Color::Green } else { Color::Yellow }))),
                ];
                f.render_widget(Paragraph::new(stats_text).wrap(Wrap { trim: false }), s);
            })?;
        }

        // Input polling 60ms for cat smoothness + engine responsiveness
        if event::poll(Duration::from_millis(45))? {
            match event::read()? {
                Event::Key(k) => {
                    // handle input buffer first for Esc/Enter/Backspace
                    match k.code {
                        KeyCode::Esc => {
                            if !input_buf.is_empty() {
                                input_buf.clear();
                                status = "Input cleared — click or type SAN".into();
                            } else if selected.is_some() {
                                selected = None; legal_dests.clear(); status = "Selection cleared".into();
                            } else {
                                // Esc also quit if nothing to clear? keep as clear
                            }
                        },
                        KeyCode::Enter => {
                            if !input_buf.is_empty() {
                                if engine_thinking {
                                    status = "😺 Neko is thinking… wait!".into();
                                } else {
                                    let mv_opt = try_san(&board, &input_buf);
                                    if let Some(mv) = mv_opt {
                                        // SAN found
                                        let san_str = san_of(&board, mv.clone());
                                        history.push(board.clone());
                                        last_move = Some(mv.clone());
                                        board = board.play(mv).unwrap();
                                        status = format!("You: {} ", san_str);
                                        if board.is_check() { status.push_str(" check!"); }
                                        input_buf.clear();
                                        selected = None; legal_dests.clear();
                                        // engine reply async — non-blocking; board will update when bestmove arrives
                                        if !board.is_game_over() {
                                            if let Some(ref eng) = engine_handle {
                                                if !engine_thinking {
                                                    let fen = Fen::from_position(&board as &Chess, EnPassantMode::Legal).to_string();
                                                    let _ = eng.tx.send(EngineCmd::Go { fen, depth: 10 });
                                                    engine_thinking = true;
                                                    status.push_str(" • 😺 thinking…");
                                                }
                                            }
                                        } else {
                                            status = format!("Game over: {:?}", board.outcome());
                                        }
                                    } else {
                                        status = format!("Illegal SAN: '{}'  (try e4, Nf3, O-O, e8=Q)", input_buf);
                                        input_buf.clear();
                                    }
                                }
                            } else {
                                // Enter with empty buffer does nothing
                            }
                        },
                        KeyCode::Backspace => {
                            if !input_buf.is_empty() {
                                input_buf.pop();
                                status = if input_buf.is_empty() { "Input cleared".into() } else { format!("SAN: {}█", input_buf) };
                            }
                        },
                        KeyCode::Char(c) => {
                            // handle commands when buffer empty and no modifiers
                            let is_ctrl = k.modifiers.contains(event::KeyModifiers::CONTROL);
                            let is_alt = k.modifiers.contains(event::KeyModifiers::ALT);
                            if is_ctrl || is_alt {
                                // ignore
                            } else if c == 'q' && input_buf.is_empty() {
                                break 'outer;
                            } else if c == 'Q' && input_buf.is_empty() {
                                // Q for queen move? treat as SAN start
                                input_buf.push(c);
                                status = format!("SAN: {}█  (Enter ↩ to play, Esc to cancel)", input_buf);
                            } else if c == 'n' && input_buf.is_empty() {
                                board = Chess::default(); history.clear(); selected=None; legal_dests.clear(); last_move=None; input_buf.clear(); status="New game — your move ✨".into(); last_eval="0.00".into(); last_depth=0; last_nodes=0; last_info_pv.clear(); engine_thinking=false;
                                // drain engine pending? send stop
                                if let Some(ref eng) = engine_handle { let _ = eng.tx.send(EngineCmd::Stop); }
                            } else if c == 'u' && input_buf.is_empty() {
                                if let Some(prev) = history.pop() {
                                    board = prev;
                                    selected=None; legal_dests.clear(); last_move=None; status="Undo ✓".into();
                                    engine_thinking=false;
                                    if let Some(ref eng) = engine_handle { let _ = eng.tx.send(EngineCmd::Stop); }
                                } else {
                                    status="Nothing to undo".into();
                                }
                            } else if c == 'f' && input_buf.is_empty() {
                                // flip not implemented - placeholder
                                status="Flip: board perspective toggle (TODO)".into();
                            } else {
                                // SAN typing: allow typical SAN chars
                                // Include KQRBN for pieces, a-h, 1-8, x, =, -, O, +, #, !, ?
                                let san_chars = "KQRBNabcdefgh12345678xO-=+#!?";
                                if san_chars.contains(c) || c == '=' || c == '+' || c == '#' || c == '!' || c == '?' || c == 'x' || c == '-' {
                                    input_buf.push(c);
                                    status = format!("SAN: {}█  (Enter ↩ to play, Esc to cancel)", input_buf);
                                    // auto-try on the fly for quick moves like e4? But require Enter to confirm to avoid mis-parse.
                                    // However allow immediate execution if input is unambiguous and length >=2 and try_san succeeds and next char not continuing? We'll wait for Enter to be safe.
                                } else if c == ' ' && !input_buf.is_empty() {
                                    // space ends input? ignore
                                } else {
                                    // other char: if it's 'q' but buffer not empty, treat as SAN? queen promotion uses Q not q, but handle lower q for quit only when empty, otherwise ignore.
                                    // For lower 'e' etc already handled via contains.
                                    // For digits already.
                                }
                                // handle lower case for SAN already via san_chars includes a-h
                            }
                        },
                        _ => {}
                    }
                },
                Event::Mouse(m) => {
                    if let MouseEventKind::Down(btn) = m.kind {
                        let _ = btn;
                        // Recompute geometry exactly as draw
                        if let Ok((cols, rows)) = crossterm::terminal::size() {
                            let area = Rect { x: 0, y: 0, width: cols, height: rows };
                            let geom = compute_geom(area);
                            // check inside board
                            if m.column >= geom.left && m.column < geom.left + geom.board_w && m.row >= geom.top && m.row < geom.top + geom.board_h {
                                if engine_thinking {
                                    status = "😺 Neko is thinking… wait!".into();
                                } else {
                                    let file_disp = (m.column - geom.left) / geom.cell_w;
                                    let rank_disp = 8 - (m.row - geom.top) / geom.cell_h;
                                    let file = if flipped { 7 - file_disp } else { file_disp };
                                    let rank = if flipped { 9 - rank_disp } else { rank_disp };
                                    if file < 8 && rank >= 1 && rank <= 8 {
                                        let sq = Square::from_coords(File::try_from(file as u8).unwrap(), Rank::try_from((rank-1) as u8).unwrap());
                                        if selected.is_none() {
                                            if let Some(p) = board.board().piece_at(sq) {
                                                if p.color == board.turn() {
                                                    selected = Some(sq);
                                                    // compute legal dests for hint
                                                    legal_dests = board.legal_moves().iter().filter_map(|mv| if mv.from()==Some(sq) { Some(mv.to()) } else { None }).collect();
                                                    status = format!("Selected {} → click destination  ({} moves)", sq, legal_dests.len());
                                                    input_buf.clear();
                                                } else {
                                                    status = format!("It's {:?}'s turn", board.turn());
                                                }
                                            } else {
                                                status = "No piece there".into();
                                            }
                                        } else {
                                            let from = selected.unwrap();
                                            let to = sq;
                                            // if clicking same color piece, reselect
                                            if let Some(p) = board.board().piece_at(sq) {
                                                if p.color == board.turn() && from != to {
                                                    selected = Some(sq);
                                                    legal_dests = board.legal_moves().iter().filter_map(|mv| if mv.from()==Some(sq) { Some(mv.to()) } else { None }).collect();
                                                    status = format!("Selected {} → click dest", sq);
                                                    continue;
                                                }
                                            }
                                            // find matching legal move (handle promotion = queen)
                                            let mut found: Option<Move> = None;
                                            for mv in board.legal_moves() {
                                                if mv.from() == Some(from) && mv.to() == to { found = Some(mv); break; }
                                            }
                                            if found.is_none() {
                                                // try promotion queen fallback
                                                let pm = Move::Normal { role: Role::Pawn, from, capture: board.board().piece_at(to).map(|p| p.role), to, promotion: Some(Role::Queen) };
                                                if board.is_legal(pm.clone()) { found = Some(pm); }
                                            }
                                            if let Some(mv) = found {
                                                if board.is_legal(mv.clone()) {
                                                    let san_str = san_of(&board, mv.clone());
                                                    history.push(board.clone());
                                                    last_move = Some(mv.clone());
                                                    board = board.play(mv).unwrap();
                                                    status = format!("You: {} ({:?}->{:?})", san_str, from, to);
                                                    if board.is_check() { status.push_str("  ☠️ CHECK!"); }
                                                    selected = None; legal_dests.clear(); input_buf.clear();
                                                    if !board.is_game_over() {
                                                        if let Some(ref eng) = engine_handle {
                                                            if !engine_thinking {
                                                                let fen = Fen::from_position(&board as &Chess, EnPassantMode::Legal).to_string();
                                                                let _ = eng.tx.send(EngineCmd::Go { fen, depth: 10 });
                                                                engine_thinking = true;
                                                                status.push_str(" • 😺 thinking…");
                                                            }
                                                        }
                                                    } else {
                                                        status = format!("Game over: {:?}", board.outcome());
                                                    }
                                                } else { status = "Illegal".into(); selected=None; legal_dests.clear(); }
                                            } else {
                                                // illegal destination
                                                if let Some(p) = board.board().piece_at(sq) {
                                                    if p.color == board.turn() { selected=Some(sq); legal_dests = board.legal_moves().iter().filter_map(|mv| if mv.from()==Some(sq) { Some(mv.to()) } else { None }).collect(); status=format!("Selected {}", sq); } else { status="Illegal move".into(); selected=None; legal_dests.clear(); }
                                                } else { status="Illegal move".into(); selected=None; legal_dests.clear(); }
                                            }
                                        }
                                    }
                                }
                            } else {
                                // click outside board: clear selection if inside stats?
                                // if click on stats area, maybe ignore
                            }
                        }
                    }
                },
                _ => {}
            }
        }
        if board.is_game_over() && !engine_thinking {
            // keep status as game over
        }
        // small throttle for cat
        if last_tick.elapsed() < Duration::from_millis(10) {
            // already handled via poll timeout
        }
        last_tick = Instant::now();
    }

    crossterm::terminal::disable_raw_mode()?;
    crossterm::execute!(io::stdout(), crossterm::terminal::LeaveAlternateScreen, crossterm::event::DisableMouseCapture)?;
    if let Some(eng) = engine_handle {
        let _ = eng.tx.send(EngineCmd::Quit);
        thread::sleep(Duration::from_millis(80));
    }
    Ok(())
}

fn rand_tick() -> bool {
    // cheap pseudo random without external crate
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};
    let now = Instant::now();
    let mut h = DefaultHasher::new();
    format!("{:?}", now).hash(&mut h);
    h.finish() % 7 == 0
}
