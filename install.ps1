# NekoClaw — One-liner for Windows 11 (PowerShell, normal user)
# irm https://raw.githubusercontent.com/thisisforlearn/nekoclaw/main/install.ps1 | iex
$ErrorActionPreference = "Stop"
Write-Host "🐾 NekoClaw — Windows 11 Installer by Vaibhav" -ForegroundColor Magenta
$REPO="thisisforlearn/nekoclaw"; $TAG="v1.0.0"
$ARCH="x86_64"
$ProgressPreference = "Continue"
# Rust
if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
  Write-Host "Installing Rust..." -ForegroundColor Cyan
  Invoke-WebRequest -UseBasicParsing https://win.rustup.rs/x86_64 -OutFile "$env:TEMP\rustup-init.exe"
  & "$env:TEMP\rustup-init.exe" -y --no-modify-path
  $env:PATH = "$env:USERPROFILE\.cargo\bin;$env:PATH"
}
Write-Host "Rust: $(cargo --version)" -ForegroundColor Green
# Try precompiled
$BIN_URL="https://github.com/$REPO/releases/download/$TAG/nekoclaw-windows-$ARCH.exe"
$DEST="$env:TEMP\nekoclaw.exe"
Write-Host "Downloading $BIN_URL ..." -ForegroundColor Cyan
try {
  Invoke-WebRequest -UseBasicParsing -Uri $BIN_URL -OutFile $DEST
  if (Test-Path $DEST) {
    Copy-Item $DEST "$env:USERPROFILE\.cargo\bin\nekoclaw.exe" -Force
    Copy-Item $DEST ".\build\nekoclaw.exe" -Force -ErrorAction SilentlyContinue
    Write-Host "✔ Installed to $env:USERPROFILE\.cargo\bin\nekoclaw.exe" -ForegroundColor Green
  }
} catch { Write-Host "Building from source..." -ForegroundColor Yellow; $NeedBuild=$true }
if ($NeedBuild) {
  git clone --depth 1 "https://github.com/$REPO.git" "$env:TEMP\nekoclaw"
  Set-Location "$env:TEMP\nekoclaw"
  cmake -S engine -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --config Release
  cargo build --release --manifest-path tui/Cargo.toml
  Copy-Item "build\Release\nekoclaw.exe" "$env:USERPROFILE\.cargo\bin\" -Force
  Copy-Item "tui\target\release\nekoclaw-tui.exe" "$env:USERPROFILE\.cargo\bin\" -Force
  Write-Host "✔ Built" -ForegroundColor Green
}
# NNUE
New-Item -ItemType Directory -Force -Path "weights" | Out-Null
try { Invoke-WebRequest -Uri "https://github.com/$REPO/releases/download/$TAG/nekoclaw-1024x8-scReLU.nnue" -OutFile "weights/nekoclaw-1024x8-scReLU.nnue" } catch {}
Write-Host "✔ Done! Run: nekoclaw-tui  or  nekoclaw --console" -ForegroundColor Green
Write-Host "Training: python scripts/train_ui.py --size 500 --mode transfer" -ForegroundColor Cyan
