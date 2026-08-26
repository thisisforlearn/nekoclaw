#!/usr/bin/env bash
# NekoClaw — One-liner for Termux (Android)
# curl -fsSL https://raw.githubusercontent.com/thisisforlearn/nekoclaw/main/install-termux.sh | bash
set -e
GREEN=$(tput setaf 2 2>/dev/null || echo ""); CYAN=$(tput setaf 6 2>/dev/null || echo ""); RESET=$(tput sgr0 2>/dev/null || echo "")
echo "🐾 NekoClaw Termux Installer — Vaibhav"
echo "SAFE — writes only to \$PREFIX/bin/nekoclaw* and ./build"
pkg update -y && pkg upgrade -y
pkg install -y curl git clang pkg-config rust python cmake
export PATH="$HOME/.cargo/bin:$PATH"
REPO="thisisforlearn/nekoclaw"; TAG="v1.0.0"
# Try precompiled termux binary
BIN_URL="https://github.com/$REPO/releases/download/$TAG/nekoclaw-termux-aarch64"
if curl -fL --progress-bar "$BIN_URL" -o /tmp/nekoclaw 2>&1; then
  chmod +x /tmp/nekoclaw
  cp /tmp/nekoclaw $PREFIX/bin/nekoclaw
  cp /tmp/nekoclaw ./build/nekoclaw 2>/dev/null || (mkdir -p build && cp /tmp/nekoclaw build/nekoclaw)
  echo "✔ Installed to $PREFIX/bin/nekoclaw"
else
  echo "Building from source (3-5 min, phone warm)..."
  git clone --depth 1 "https://github.com/$REPO.git" /tmp/nekoclaw-src
  cd /tmp/nekoclaw-src
  cmake -S engine -B build -DCMAKE_BUILD_TYPE=Release -DUSE_AVX2=OFF
  cmake --build build -j2
  cargo build --release --manifest-path tui/Cargo.toml
  cp build/nekoclaw $PREFIX/bin/nekoclaw
  cp tui/target/release/nekoclaw-tui $PREFIX/bin/nekoclaw-tui
  echo "✔ Built"
fi
mkdir -p weights
curl -fL --progress-bar "https://github.com/$REPO/releases/download/$TAG/nekoclaw-1024x8-scReLU.nnue" -o weights/nekoclaw-1024x8-scReLU.nnue || echo "No release yet"
echo "✔ Done! Run: nekoclaw-tui  (touch = click)  or  nekoclaw --console"
echo "If TUI flickers: nekoclaw --console"
# Termux wakelock hint
echo "Tip: termux-wake-lock to keep building when screen off"
