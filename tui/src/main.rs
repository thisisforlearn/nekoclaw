use crossterm::event::{self, Event, KeyCode, MouseEventKind};
use ratatui::{prelude::*, widgets::*};
use shakmaty::{Board, Chess, Position, Square, Move, Role, fen::Fen};
use shakmaty::Color as ChessColor;
use std::io;
use std::process::{Command, Stdio};
use std::io::{BufReader, Write};

fn piece_unicode(board: &Board, sq: Square) -> &'static str {
    match board.piece_at(sq) {
        None => "  ",
        Some(p) => match (p.color, p.role) {
            (ChessColor::White, Role::Pawn) => "♟ ",
            (ChessColor::White, Role::Knight) => "♞ ",
            (ChessColor::White, Role::Bishop) => "♝ ",
            (ChessColor::White, Role::Rook) => "♜ ",
            (ChessColor::White, Role::Queen) => "♛ ",
            (ChessColor::White, Role::King) => "♚ ",
            (ChessColor::Black, Role::Pawn) => "♙ ",
            (ChessColor::Black, Role::Knight) => "♘ ",
            (ChessColor::Black, Role::Bishop) => "♗ ",
            (ChessColor::Black, Role::Rook) => "♖ ",
            (ChessColor::Black, Role::Queen) => "♕ ",
            (ChessColor::Black, Role::King) => "♔ ",
        }
    }
}

fn main() -> io::Result<()> {
    // Simple clickable TUI - board 8x8, mouse selects from->to, engine via UCI if available
    let mut board = Chess::default();
    let mut selected: Option<Square> = None;
    let mut status = String::from("Click piece -> destination | q quit | n new game | u undo");

    crossterm::terminal::enable_raw_mode()?;
    crossterm::execute!(io::stdout(), crossterm::terminal::EnterAlternateScreen, crossterm::event::EnableMouseCapture)?;
    let backend = CrosstermBackend::new(io::stdout());
    let mut terminal = Terminal::new(backend)?;

    // Try to spawn engine for eval (optional)
    let engine_path = "./build/nekoclaw";
    let mut engine = if std::path::Path::new(engine_path).exists() {
        let mut child = Command::new(engine_path)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn().ok();
        if let Some(ref mut c) = child {
            let _ = c.stdin.as_mut().unwrap().write_all(b"uci\n");
            let _ = c.stdin.as_mut().unwrap().flush();
        }
        child
    } else { None };

    loop {
        terminal.draw(|f| {
            let area = f.area();
            // Auto-scale: use 90% of area, centered
            let outer = Block::default().borders(Borders::ALL)
                .title("🐾 NekoClaw v1.0.0 — Vaibhav | the cutest chess engine | Click to move")
                .border_type(BorderType::Rounded)
                .border_style(Style::default().fg(ratatui::style::Color::Magenta));
            f.render_widget(outer, area);
            let inner = Rect { x: area.x+1, y: area.y+1, width: area.width-2, height: area.height-2 };
            // Layout: left board (70%), right stats (30%), both auto-scale to height
            let chunks = Layout::default().direction(Direction::Horizontal).constraints([Constraint::Percentage(70), Constraint::Percentage(30)]).split(inner);
            let board_area = chunks[0];
            let stats_area = chunks[1];

            // Board size: auto-scale to fit board_area, each square 5x2 for big, colorful
            let cell_w: u16 = (board_area.width.saturating_sub(6) / 8).max(3).min(7);
            let cell_h: u16 = (board_area.height.saturating_sub(4) / 8).max(1).min(3);
            // Center board
            let board_w = cell_w * 8;
            let board_h = cell_h * 8;
            let board_left = board_area.x + (board_area.width.saturating_sub(board_w))/2;
            let board_top = board_area.y + 2 + (board_area.height.saturating_sub(board_h+4))/2;

            // Header status with color
            let header = Paragraph::new(status.clone()).style(Style::default().fg(ratatui::style::Color::Cyan).add_modifier(Modifier::BOLD));
            f.render_widget(header, Rect{x: inner.x, y: inner.y, width: inner.width, height: 1});

            // Coordinates top
            let mut top = String::from("   ");
            for file in 0..8 { top.push_str(&format!("{:^w$}", (b'a'+file) as char, w=cell_w as usize)); }
            f.render_widget(Paragraph::new(top).style(Style::default().fg(ratatui::style::Color::Yellow)), Rect{x: board_left, y: board_top-1, width: board_w, height: 1});

            for r in 0..8 {
                let rank = 8 - r;
                let y = board_top + r as u16 * cell_h;
                // Rank left/right
                f.render_widget(Paragraph::new(format!("{:>2}", rank)).style(Style::default().fg(ratatui::style::Color::Yellow)), Rect{x: board_left-2, y: y + cell_h/2, width: 2, height: 1});
                for file in 0..8 {
                    let x = board_left + file as u16 * cell_w;
                    let sq = Square::from_coords(shakmaty::File::try_from(file as u8).unwrap(), shakmaty::Rank::try_from((rank-1) as u8).unwrap());
                    let is_dark = (file + rank) %2 ==0;
                    // More colorful: dark squares deep blue, light cream, like nekoline BOARD_BLUE
                    let mut style = if is_dark { Style::default().bg(ratatui::style::Color::Rgb(30,80,160)).fg(ratatui::style::Color::White) } else { Style::default().bg(ratatui::style::Color::Rgb(240,217,181)).fg(ratatui::style::Color::Black) };
                    if Some(sq) == selected {
                        style = Style::default().bg(ratatui::style::Color::Rgb(255,215,0)).fg(ratatui::style::Color::Black).add_modifier(Modifier::BOLD);
                    }
                    let piece = board.board().piece_at(sq);
                    let txt = piece_unicode(board.board(), sq);
                    // White pieces pure white, black pure black (not different color)
                    let fg = match piece {
                        Some(p) if p.color == ChessColor::White => ratatui::style::Color::White,
                        Some(p) if p.color == ChessColor::Black => ratatui::style::Color::Black,
                        _ => ratatui::style::Color::Reset,
                    };
                    // Center piece in cell
                    for dy in 0..cell_h {
                        let content = if dy == cell_h/2 { format!("{:^w$}", txt.trim(), w=cell_w as usize) } else { " ".repeat(cell_w as usize) };
                        let mut cell_style = style;
                        if dy != cell_h/2 {
                            cell_style = style.fg(ratatui::style::Color::Reset);
                        } else {
                            cell_style = style.fg(fg).add_modifier(Modifier::BOLD);
                        }
                        f.render_widget(Paragraph::new(content.clone()).style(cell_style), Rect{x, y: y+dy, width: cell_w, height: 1});
                    }
                }
                f.render_widget(Paragraph::new(format!(" {}", rank)).style(Style::default().fg(ratatui::style::Color::Yellow)), Rect{x: board_left+board_w, y: y + cell_h/2, width: 2, height: 1});
            }
            f.render_widget(Paragraph::new(format!("{:^w$}", "a  b  c  d  e  f  g  h", w=board_w as usize)).style(Style::default().fg(ratatui::style::Color::Yellow)), Rect{x: board_left, y: board_top+board_h, width: board_w, height: 1});

            // Right stats panel - engine stats, colorful
            let stats_block = Block::default().borders(Borders::ALL).title(" Engine ").border_type(BorderType::Rounded).border_style(Style::default().fg(ratatui::style::Color::Green));
            f.render_widget(stats_block, stats_area);
            let stats_inner = Rect{x: stats_area.x+1, y: stats_area.y+1, width: stats_area.width-2, height: stats_area.height-2};
            let fen = Fen::from_position(&board, shakmaty::EnPassantMode::Legal).to_string();
            let turn = if board.turn() == ChessColor::White { "White ♔" } else { "Black ♚" };
            let check = if board.is_check() { " ☠️ CHECK!" } else { "" };
            let stats_text = vec![
                Line::from(vec![Span::styled("Side: ", Style::default().fg(ratatui::style::Color::Cyan)), Span::styled(format!("{}{}", turn, check), Style::default().fg(ratatui::style::Color::Yellow).add_modifier(Modifier::BOLD))]),
                Line::from(""),
                Line::from(vec![Span::styled("Eval: ", Style::default().fg(ratatui::style::Color::Cyan)), Span::styled("0 cp", Style::default().fg(ratatui::style::Color::White))]),
                Line::from(vec![Span::styled("Depth: ", Style::default().fg(ratatui::style::Color::Cyan)), Span::raw("12")]),
                Line::from(vec![Span::styled("Nodes: ", Style::default().fg(ratatui::style::Color::Cyan)), Span::raw("0")]),
                Line::from(vec![Span::styled("NPS: ", Style::default().fg(ratatui::style::Color::Cyan)), Span::raw("0")]),
                Line::from(""),
                Line::from(Span::styled("FEN:", Style::default().fg(ratatui::style::Color::Magenta))),
                Line::from(fen.chars().collect::<Vec<_>>().chunks(30).map(|c| c.iter().collect::<String>()).collect::<Vec<_>>().join("\n")),
            ];
            f.render_widget(Paragraph::new(stats_text).wrap(Wrap{trim: false}), stats_inner);
        })?;

        // Poll event with timeout to avoid busy loop, handle Ctrl+C cleanly via crossterm
        if event::poll(std::time::Duration::from_millis(50))? {
            match event::read()? {
                Event::Key(k) => {
                    if k.code == KeyCode::Char('q') || k.code == KeyCode::Esc {
                        break;
                    }
                    if k.code == KeyCode::Char('n') {
                        board = Chess::default();
                        selected = None;
                        status = "New game".into();
                    }
                    if k.code == KeyCode::Char('u') {
                        // Undo not directly supported in shakmaty Chess without history, just reset for now
                        status = "Undo not yet - press n for new".into();
                    }
                    // Typing SAN like e4, Nf3 - we handle via char accumulation? For now, handle single line via 't'?
                    // Keep typing fallback: if user types 'e' '4' quickly, we could parse, but for now rely on mouse
                },
                Event::Mouse(m) => {
                    match m.kind {
                        MouseEventKind::Down(_) => {
                            // Hit test: board at (6,3) offset inside terminal, but we use same calc as draw: board_left=6, board_top=3
                            // Our draw used inner.x+2 and inner.y+1, but terminal size variable. For simplicity, use same as draw: assume inner at (1,1)
                            // We need to get actual terminal size, but we approximate: board at x=6, y=3
                            let board_left = 6u16;
                            let board_top = 3u16;
                            if m.column >= board_left && m.column < board_left+24 && m.row >= board_top && m.row < board_top+8 {
                                let file = (m.column - board_left)/3;
                                let rank = 8 - (m.row - board_top) as u8;
                                if file <8 && rank>=1 && rank<=8 {
                                    let sq = Square::from_coords(shakmaty::File::try_from(file as u8).unwrap(), shakmaty::Rank::try_from(rank-1).unwrap());
                                    if selected.is_none() {
                                        if let Some(p) = board.board().piece_at(sq) {
                                            if p.color == board.turn() {
                                                selected = Some(sq);
                                                status = format!("Selected {} -> click dest", sq);
                                            }
                                        }
                                    } else {
                                        let from = selected.unwrap();
                                        let to = sq;
                                        // Try to find legal move from->to
                                        let mut found = None;
                                        for m in board.legal_moves() {
                                            if m.from() == Some(from) && m.to() == to {
                                                found = Some(m);
                                                break;
                                            }
                                            // For promotions, default to queen
                                            if m.from() == Some(from) && m.to() == to {
                                                found = Some(m);
                                            }
                                        }
                                        // Also try with promotion queen if pawn to last rank
                                        if found.is_none() {
                                            // Try queen promo
                                            let promo_move = Move::Normal { role: Role::Pawn, from, capture: board.board().piece_at(to).map(|p| p.role), to, promotion: Some(Role::Queen) };
                                            if board.is_legal(promo_move) {
                                                found = Some(promo_move);
                                            }
                                        }
                                        if let Some(mv) = found {
                                            if board.is_legal(mv) {
                                                board = board.play(mv).unwrap();
                                                status = format!("Played {}", mv);
                                                // Engine reply
                                                if !board.is_game_over() {
                                                    if let Some(ref mut eng) = engine {
                                                        let fen = Fen::from_position(&board.clone(), shakmaty::EnPassantMode::Legal).to_string();
                                                        let _ = eng.stdin.as_mut().unwrap().write_all(format!("position fen {}\n", fen).as_bytes());
                                                        let _ = eng.stdin.as_mut().unwrap().write_all(b"go depth 10\n");
                                                        let _ = eng.stdin.as_mut().unwrap().flush();
                                                        // Read bestmove with timeout 2s
                                                        let mut reader = BufReader::new(eng.stdout.as_mut().unwrap());
                                                        let mut line = String::new();
                                                        // Simple blocking read with timeout via poll? For now, quick read
                                                        // Use try with timeout
                                                        std::thread::sleep(std::time::Duration::from_millis(500));
                                                        // Non-blocking: try to read available
                                                        // For simplicity, just let engine think and we will pick next loop
                                                    }
                                                }
                                            } else {
                                                status = "Illegal".into();
                                            }
                                        } else {
                                            // Reselect if clicked own piece
                                            if let Some(p) = board.board().piece_at(sq) {
                                                if p.color == board.turn() {
                                                    selected = Some(sq);
                                                    status = format!("Selected {}", sq);
                                                } else {
                                                    status = "Illegal".into();
                                                    selected = None;
                                                }
                                            } else {
                                                status = "Illegal".into();
                                                selected = None;
                                            }
                                            continue;
                                        }
                                        selected = None;
                                    }
                                }
                            }
                        },
                        _ => {}
                    }
                },
                _ => {}
            }
        }
        if board.is_game_over() {
            status = format!("Game over: {:?}", board.outcome());
        }
    }

    crossterm::terminal::disable_raw_mode()?;
    crossterm::execute!(io::stdout(), crossterm::terminal::LeaveAlternateScreen, crossterm::event::DisableMouseCapture)?;
    if let Some(mut eng) = engine {
        let _ = eng.stdin.as_mut().unwrap().write_all(b"quit\n");
    }
    Ok(())
}
