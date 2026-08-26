#!/usr/bin/env bash
# NekoClaw — One-liner installer for Linux (Debian/Ubuntu/Fedora/Arch)
# Author: vaibhav · License: GPL-3.0-only (commercial via author)
# Usage (copy-paste, plug-and-play for someone who never opened terminal):
#   curl -fsSL https://raw.githubusercontent.com/thisisforlearn/nekoclaw/main/install.sh | bash
# Does everything: installs deps, Rust if missing, downloads binary or builds from source, with progress bar.
set -e
if command -v tput >/dev/null 2>&1 && [ -t 1 ]; then
  GREEN=$(tput setaf 2); CYAN=$(tput setaf 6); YELLOW=$(tput setaf 3); RED=$(tput setaf 1); BOLD=$(tput bold); RESET=$(tput sgr0)
else
  GREEN=""; CYAN=""; YELLOW=""; RED=""; BOLD=""; RESET=""
fi
progress() { echo "${CYAN}▸${RESET} $1"; }
success() { echo "${GREEN}✔${RESET} $1"; }
warn() { echo "${YELLOW}⚠${RESET} $1"; }
fail() { echo "${RED}✘${RESET} $1"; exit 1; }
bar_download() {
  local url="$1" dest="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --progress-bar "$url" -o "$dest"
  elif command -v wget >/dev/null 2>&1; then
    wget --show-progress -O "$dest" "$url"
  else
    fail "Need curl or wget. Try: sudo apt install curl"
  fi
}
if command -v tput >/dev/null 2>&1 && [ -t 1 ]; then HAS_TPUT=1; else HAS_TPUT=0; fi
draw_bottom(){
  local msg="$1" pct="$2"
  local bar_len=30 filled=$((pct*bar_len/100)) empty=$((bar_len-filled))
  local bar=$(printf "%${filled}s" "" | tr ' ' '█')
  local ebar=$(printf "%${empty}s" "" | tr ' ' '·')
  if [ "$HAS_TPUT" -eq 1 ] && [ -t 1 ]; then
    tput sc; tput cup $(tput lines) 0; tput el
    printf "${CYAN}▓${RESET} ${BOLD}%s${RESET} [${GREEN}%s${RESET}%s] %3s%%" "$msg" "$bar" "$ebar" "$pct"
    tput rc
  else
    printf "\r${CYAN}▓${RESET} %s [%s%s] %3s%%" "$msg" "$bar" "$ebar" "$pct"
  fi
}
clear_bottom(){ if [ "$HAS_TPUT" -eq 1 ] && [ -t 1 ]; then tput sc; tput cup $(tput lines) 0; tput el; tput rc; else echo ""; fi; }
trap 'fail "Installer failed at line $LINENO"; clear_bottom' ERR
echo "${BOLD}${CYAN}"
cat <<'BANNER'
 ╱|、 NekoClaw — the cutest chess engine =^._.^=
 One-liner Linux Installer — by Vaibhav
BANNER
echo "${RESET}"
CURRENT_DIR="$(pwd)"
echo "SAFE — will write ONLY to:"
echo "  • CURRENT: ${CYAN}$CURRENT_DIR/nekoclaw${RESET} + $CURRENT_DIR/build, $CURRENT_DIR/tui/target"
echo "  • ~/.local/bin/nekoclaw, nekoclaw-tui"
echo "  • Rust to ~/.cargo if missing"
echo ""
if [ -t 0 ]; then
  read -p "Proceed? [Y/n]: " ans
  case "$ans" in [nN]*) echo "Cancelled."; exit 0;; esac
fi
draw_bottom "Starting — Vaibhav" 5
progress "Checking internet..."
draw_bottom "Pinging..." 8
check_ping(){ ping -c 1 -W 2 "$1" >/dev/null 2>&1 && echo "  ${GREEN}✔${RESET} $1" || echo "  ${RED}✘${RESET} $1"; }
PING_OK=0; check_ping "8.8.8.8" && PING_OK=$((PING_OK+1)); check_ping "1.1.1.1" && PING_OK=$((PING_OK+1))
draw_bottom "Internet $PING_OK/2" 10
progress "Detecting OS..."
OS="linux"; ARCH=$(uname -m)
case "$ARCH" in x86_64|amd64) ARCH="x86_64";; aarch64|arm64) ARCH="aarch64";; *) ARCH="x86_64";; esac
success "Linux $ARCH"
progress "Installing deps..."
if command -v apt-get >/dev/null 2>&1; then
  if [ "$EUID" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi
  $SUDO apt-get update -qq
  $SUDO apt-get install -y -qq curl git build-essential pkg-config cmake python3 python3-pip 2>&1 | stdbuf -oL tr '\r' '\n' | while read -r line; do echo "  $line"; done || true
elif command -v dnf >/dev/null 2>&1; then
  sudo dnf install -y curl git gcc pkg-config cmake python3 pip
elif command -v pacman >/dev/null 2>&1; then
  sudo pacman -Sy --noconfirm curl git base-devel cmake python
else
  warn "No apt/dnf/pacman, assuming deps present"
fi
success "Deps ready"
if ! command -v cargo >/dev/null 2>&1; then
  progress "Installing Rust..."
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path --profile minimal
  export PATH="$HOME/.cargo/bin:$PATH"
  [ -f "$HOME/.cargo/env" ] && . "$HOME/.cargo/env"
  success "Rust $(rustc --version)"
else
  success "Rust: $(rustc --version)"
fi
export PATH="$HOME/.cargo/bin:$PATH"
REPO="thisisforlearn/nekoclaw"
TAG="v1.0.0"
BIN_URL="https://github.com/$REPO/releases/download/$TAG/nekoclaw-linux-$ARCH"
DEST="/tmp/nekoclaw"
progress "Trying precompiled $BIN_URL ..."
draw_bottom "Downloading..." 60
set +e; bar_download "$BIN_URL" "$DEST" 2>&1; DL_OK=$?; set -e
NEED_BUILD=1
if [ $DL_OK -eq 0 ] && [ -s "$DEST" ]; then
  chmod +x "$DEST"
  if "$DEST" --help >/dev/null 2>&1 || "$DEST" bench 2>&1 | head -1 | grep -q "NekoClaw"; then
    success "Downloaded precompiled"
    draw_bottom "Downloaded" 70
    mkdir -p "$HOME/.local/bin"
    cp "$DEST" "$HOME/.local/bin/nekoclaw"
    cp "$DEST" "./build/nekoclaw" 2>/dev/null || (mkdir -p build && cp "$DEST" build/nekoclaw)
    chmod +x "./build/nekoclaw" 2>/dev/null || true
    NEED_BUILD=0
  fi
fi
if [ $NEED_BUILD -eq 1 ]; then
  warn "Building from source (3-5 min, progress bar)..."
  TMPDIR=$(mktemp -d)
  git clone --depth 1 "https://github.com/$REPO.git" "$TMPDIR/nekoclaw" 2>&1 | while read -r line; do echo "  $line"; done
  cd "$TMPDIR/nekoclaw"
  # C++ engine
  cmake -S engine -B build -DCMAKE_BUILD_TYPE=Release -DUSE_AVX2=ON 2>&1 | sed 's/^/  /'
  cmake --build build -j$(nproc) 2>&1 | stdbuf -oL sed 's/^/  /' &
  CARGO_PID=$!
  i=0; sp="⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏"
  while kill -0 $CARGO_PID 2>/dev/null; do printf "\r${CYAN}%s${RESET} Building C++... %ds" "${sp:i++%${#sp}:1}" "$SECONDS"; sleep 0.15; done
  wait $CARGO_PID; echo ""
  # Rust TUI
  cargo build --release --manifest-path tui/Cargo.toml 2>&1 | stdbuf -oL sed 's/^/  /' &
  CARGO_PID=$!
  while kill -0 $CARGO_PID 2>/dev/null; do printf "\r${CYAN}%s${RESET} Building TUI... %ds" "${sp:i++%${#sp}:1}" "$SECONDS"; sleep 0.15; done
  wait $CARGO_PID; echo ""
  success "Build finished"
  mkdir -p "$HOME/.local/bin"
  cp build/nekoclaw "$HOME/.local/bin/nekoclaw" 2>/dev/null || true
  cp tui/target/release/nekoclaw-tui "$HOME/.local/bin/nekoclaw-tui" 2>/dev/null || true
  mkdir -p "$CURRENT_DIR/build" && cp build/nekoclaw "$CURRENT_DIR/build/" 2>/dev/null || true
  mkdir -p "$CURRENT_DIR/tui/target/release" && cp tui/target/release/nekoclaw-tui "$CURRENT_DIR/tui/target/release/" 2>/dev/null || true
  rm -rf "$TMPDIR"
fi
# Download pre-trained NNUE with progress bar always at bottom
progress "Downloading pre-trained NNUE (50MB)..."
draw_bottom "Downloading NNUE..." 85
mkdir -p weights
bar_download "https://github.com/$REPO/releases/download/$TAG/nekoclaw-1024x8-scReLU.nnue" "weights/nekoclaw-1024x8-scReLU.nnue" 2>&1 || warn "No pre-trained release yet, will use random net (train with scripts)"
draw_bottom "NNUE ready" 90
if ! echo "$PATH" | grep -q "$HOME/.local/bin"; then
  for RC in "$HOME/.bashrc" "$HOME/.zshrc"; do [ -f "$RC" ] && ! grep -q ".local/bin" "$RC" && echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$RC" && success "Added PATH to $RC"; done
fi
echo ""
echo "${BOLD}${GREEN}✔ All done!${RESET} Run:"
echo "  ${CYAN}nekoclaw-tui${RESET}          # clickable TUI — click squares, f flip, c play as Black, q quit"
echo "  ${CYAN}nekoclaw --console${RESET}   # typed — e4, Nf3, O-O"
echo "  ${CYAN}python scripts/web_gui.py${RESET} # web on :8080 for exotic OS"
echo ""
echo "Training (normal person, progress bar):"
echo "  ${CYAN}python scripts/train_ui.py --size 500 --mode transfer${RESET}  # 50/100/500MB/1GB/5GB/custom, shows time estimate + Elo"
echo "Nerd: ${CYAN}./build/nekoclaw bench${RESET}  ${CYAN}python -m nekoclaw_trainer.train --help${RESET}"
echo "Author Vaibhav holds BDFL. GPL-3.0; commercial via https://github.com/$REPO/issues"
clear_bottom
echo "Progress bar was ALWAYS at bottom. Safe: no admin. Uninstall: rm ./build/nekoclaw ~/.local/bin/nekoclaw*"
