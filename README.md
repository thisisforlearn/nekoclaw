# NekoClaw 🐾♟️

**Author:** Vaibhav  
**License:** GNU AGPLv3 + Commercial License (contact author)  
**Language:** Highly optimized C++17 (AVX2 / AVX-VNNI where available) + Python only where >5% faster (PyTorch trainer)  
**Network:** Real NNUE from scratch — **HalfKAv2_hm · 8 buckets · L1=1024 SCReLU** — hybrid i16/i8 quantized  
**Search:** Full-stack minimax — PVS, iterative deepening + aspiration, TT, QSearch, NMP, LMR, history/killers/counters, SEE, SMP-ready  
**Use:** UCI + console play  
**Training:** PyTorch trainer for **2× NVIDIA T4 on Kaggle** (DDP) + local CPU on Debian WSL2 — lossless pause/resume

> Not a Stockfish fine-tune. Entire pipeline is independent: custom data pipeline, custom network, custom search. Stockfish/Lc0 are only used as *teachers* for labeling positions.

---

## Architecture at a glance

```
Input: HalfKAv2_hm (king-relative, horizontally-mirrored)
  King bucket = 32 (mirrored to files e..h)
  Piece types = 12 (P,N,B,R,Q,K × 2 colors)
  Feature dims = 32 × 12 × 64 = 24576

FeatureTransformer: 24576 → 1024 per perspective (AVX2, i16, QA=255)
  → dual accumulator (white/black), incrementally updated

Buckets: 8 selected by (popcount(occupancy)-1)/4   [0..7]

For each bucket b:
  L1: 2048 (1024×2 after SCReLU) → 16  (i8, QB=64)
  L2: 16 → 32 ReLU
  L3: 32 → 32 ReLU
  Out: 32 → 1
PSQT bypass: none — pure NNUE; material buckets capture phase.

Quantization: FT QA=255, hidden QB=64, output QC=64, scale 400cp
Activation: SCReLU(x)=clamp(x,0,1)^2  (squared clipped ReLU) on FT output
Inference: AVX2 (256-bit) + VNNI-int8 fast path on i5-1335U; hybrid i16/i8
```

***Why HalfKAv2_hm?*** King-relative sparse features exploit NNUE incremental updates (only ~30 adds/subs per move), mirroring halves king buckets (64→32), AVX2-friendly.

---

## Project layout

```
engine/                 # C++ engine
  include/nekoclaw/     # headers (types, board, movegen, nnue, search, uci)
  src/                  # optimized sources (AVX2 intrinsics in nnue/simd.cpp)
  CMakeLists.txt
trainer/                # Python PyTorch trainer
  nekoclaw_trainer/
    model.py            # mirrors C++ arch exactly (export parity tests)
    dataset.py          # custom binary .bin loader, 2.5M positions/chunk
    train.py            # DDP + AMP + lossless checkpoint
    export.py           # → .nnue (little-endian, versioned)
  configs/default.yaml
scripts/
  download_gm.py        # Lichess Elite DB + GM high-quality PGN fetch
  annotate.py           # teacher labeling (Stockfish depth≥22 / Lc0 nodes)
  convert_pgn_to_bin.py # PGN → custom .bin (see format below)
kaggle/
  kaggle_train.ipynb    # copy to Kaggle, 2×T4 DDP launch
weights/                # .nnue files (not in git)
tests/                  # perft & nnue parity
build/                  # CMake out-of-source build dir
```

---

## Build (Debian 13 Trixie, WSL2, AVX2)

```bash
cmake -S engine -B build -DCMAKE_BUILD_TYPE=Release -DUSE_AVX2=ON -DNATIVE_TUNING=OFF
cmake --build build -j$(nproc)
./build/nekoclaw bench        # verifies perft + nnue + search
./build/nekoclaw              # UCI mode
./build/nekoclaw --console    # human console play
```

Flags:
- `USE_AVX2=ON` (default AUTO detects AVX2; i5-1335U confirmed AVX2+VNNI)
- `USE_AVXVNNI=ON` for VPDPBUSD fast int8 path (if compiler supports -mavxvnni)
- `CMAKE_BUILD_TYPE=Release` enables `-O3 -march=x86-64-v3 -flto -DNDEBUG`

Python only where >5% faster: **PyTorch trainer** (Python is 50× faster to iterate than hand-rolled CUDA; C++ inference is 8–12× faster than Python, so engine stays C++). Per rule, Python path is only used for training.

---

## Training data format (custom binary, recommended)

```
Header (32 bytes):
  magic "NCBIN\x00\x02" (8B) | version 2 (u32) | num_positions (u64)
  | arch_hash (u32) | quantization (u32) | reserved 8B

Per position (40 bytes + cheap):
  occupancies bitboards? No — we store compact:
  fen_string_len (u16) + fen (variable) OR
  binary board: 64 bytes (piece codes) + side(1) + castle(1) + ep(1) + ply(2)
  score (i16 centipawns, teacher) | result (i8: -1,0,1) | bucket (u8) | padding
  → writer packs as 40B aligned. Reader memory-maps.
```

Why custom binary vs PGN? 4–8× faster loading, deterministic shuffling, `mmap` friendly on Kaggle.

Alternate: `--format pgn` supported via `scripts/convert_pgn_to_bin.py`.

---

## Training: how to resume losslessly

Checkpoints store **everything**:

```
checkpoint/
  epoch_042.pt               # model + optimizer + scheduler + scaler
  rng.pt                     # python / numpy / torch RNG states
  dataloader.pt              # exact shard + offset + shuffle permutation
  trainer_state.json         # global_step, epoch, best_loss
```

Resume:

```bash
# Local CPU (your laptop)
python -m nekoclaw_trainer.train --config trainer/configs/default.yaml --resume checkpoints/last.pt

# Kaggle 2×T4 (DDP auto)
torchrun --nproc_per_node=2 -m nekoclaw_trainer.train --config configs/kaggle.yaml --resume /kaggle/input/nekoclaw-ckpt/last.pt
```

Pause: `p` or `SIGUSR1` triggers graceful save; `SIGTERM` (Kaggle preemption) is caught; no loss.

Loss: `MSE` on teacher cp + `1e-3 * L2` + bucket balancing; WDL head optional.

---

## Kaggle 2×T4 usage

1. Upload dataset: `data/*.bin` as Kaggle Dataset
2. Copy `kaggle/kaggle_train.ipynb` to Kaggle Notebook
3. Enable 2×T4 (Settings → Accelerator → GPU T4 ×2)
4. Phone + face verification already done (per your setup)
5. Run — DDP spawns 2 workers, AMP fp16, batch 16384
6. Export `.nnue` at end: `python -m nekoclaw_trainer.export --ckpt last.pt --out nekoclaw-1024x8-scReLU.nnue`

---

## Data pipeline (GM + heavy teacher analysis)

You said: *have own data first, then Lichess Elite DB, plus script that downloads GM games with full computer analysis (Stockfish / Lc0 depth)* — this is implemented.

```bash
# 1) Elite DB (use your copy)
python scripts/download_gm.py --source elite --elite-path /path/to/lichess_elite_db.pgn.zst --out data/raw/

# 2) Fetch fresh GM games (2700+)
python scripts/download_gm.py --source lichess --min-elo 2700 --max-games 50000 --out data/raw/

# 3) Heavy analysis (this is the important step)
python scripts/annotate.py --in data/raw/gm.pgn --out data/labeled.epd \
  --engine stockfish --engine-path /usr/bin/stockfish --depth 22 --threads 8 \
  --hash 2048 --multipv 1 --filter-eco

# 4) Convert to custom binary for trainer
python scripts/convert_pgn_to_bin.py --in data/labeled.epd --out data/train.bin --shard 2500000
```

Annotate uses at least depth 22 (configurable) and keeps PV score after search; also supports Lc0 via `--engine lc0`.

---

## UCI

```
uci
isready
position startpos moves e2e4 e7e5
go depth 18
go wtime 300000 btime 300000 winc 5000 binc 5000
setoption name Hash value 256
setoption name Threads value 4
setoption name NNUEFile value weights/nekoclaw-1024x8-scReLU.nnue
quit
```

Console play: `./build/nekoclaw --console` (supports `e4`/`Nf3`/`O-O` or `e2e4`, click in TUI)

TUI (clickable, Linux/Win11/Termux): `./tui/target/release/nekoclaw-tui` (`f` flip, `c` play as Black, `q` quit)

Web (exotic OS, lichess-like): `python scripts/web_gui.py --port 8080` → `http://localhost:8080` (click, flip, pure NekoClaw proof at `/proof`)

---

## One-liners — for someone who never opened terminal (progress bar always at bottom, downloads pre-trained engine)

**Linux Debian/Ubuntu:**
```bash
curl -fsSL https://raw.githubusercontent.com/thisisforlearn/nekoclaw/main/install.sh | bash
# Does: apt update, installs curl/git/rust if missing, tries precompiled nekoclaw-linux-x86_64 (curl --progress-bar) or builds from source (cargo spinner), downloads weights/nekoclaw-1024x8-scReLU.nnue (50MB), sets PATH
```

**Windows 11 (PowerShell as normal user):**
```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned -Force
irm https://raw.githubusercontent.com/thisisforlearn/nekoclaw/main/install.ps1 | iex
```

**Android Termux:**
```bash
curl -fsSL https://raw.githubusercontent.com/thisisforlearn/nekoclaw/main/install-termux.sh | bash
# pkg update, clang, rust, tries nekoclaw-termux-aarch64 or builds, --tt-log2 20 for 2GB RAM
```

All show `▓ [█████····]  60%` bar at bottom via `curl --progress-bar` / `cargo` spinner + `tput`. Safe: writes only to `./build`, `~/.local/bin`, `~/.cargo`.

---

## Nerd Edition — everything in detail

```bash
# Build
cmake -S engine -B build -DCMAKE_BUILD_TYPE=Release -DUSE_AVX2=ON && cmake --build build -j
cargo build --release --manifest-path tui/Cargo.toml  # 1.5M TUI
# Bench (real, not fake)
./build/nekoclaw bench  # perft 6 119M + search
./build/perft  # perft 1-6 OK
# Training UI (normal person)
python scripts/train_ui.py --size 500 --mode transfer  # 50/100/500MB/1GB/5GB/custom, shows time estimate via bench nps
python scripts/train_ui.py --size 1000 --mode scratch
# Manual training
PYTHONPATH=trainer:$PYTHONPATH python -m nekoclaw_trainer.train --config trainer/configs/default.yaml
# Nerd fix + sharing
python scripts/nerd.py        # checks perft, missing build/data, auto-fixes
python scripts/share.py --ckpt checkpoints/best.pt --data-mb 500  # estimates Elo, pushes to GitHub weights/
```

---

## Common Errors & Fixes

| Error | Fix |
|-------|-----|
| `ModuleNotFoundError: nekoclaw_trainer` | `PYTHONPATH=trainer:$PYTHONPATH python -m nekoclaw_trainer.train ...` or `pip install -e trainer` (needs setup.py) |
| `Weights only load failed` / `weights_only` | `trainer/nekoclaw_trainer/export.py` already uses `weights_only=False` (PyTorch 2.6) — pull latest |
| `f`/`c` flip not working, pieces small | `cd ~/nekoclaw && git pull && cd tui && cargo build --release` — you had old `1.4M` binary, new is `1.5M` with `f`/`c` |
| `e4 unknown` | Use `e4`/`Nf3`/`O-O` now works (SAN) or `e2e4` (UCI) — old console needed `move e2e4` |
| `perft 5 FAIL` | Fixed `pawn double-push evasion` + `en-passant` + `ep square` — pull latest, `cmake --build build -j` |
| `Ctrl+C` doesn't save | `kill -USR1 $(pgrep -f nekoclaw_trainer.train)` saves lossless (`last.pt`); `Ctrl+C` now also saves after patch |
| `RAM 4.7GB` swap even with free | `swappiness 60` normal — `mmap 577MB` + `8.3M` shuffle list, not OOM |
| `2.6s/it` on `2×T4` slow | Set `trainer/train.py` `num_workers=4 pin_memory=True` for GPU (was `0/False` for CPU) |
| `FileNotFoundError: data/train.bin` | `!mkdir -p data` before `open` — fixed |
| `No such file scripts/nekoclaw_gui.py` | Now `scripts/web_gui.py` (web) + `tui/` (Rust) — use `python scripts/web_gui.py` |
| `gh: command not found` | `sudo apt install gh && gh auth login` or use PAT `git push https://<PAT>@github.com/...` |
| `HF unauthenticated` slow | Kaggle `Settings` → `Secrets` → `HF_TOKEN` from `huggingface.co/settings/tokens` |

---

## Benchmarks (target on i5-1335U)

- Perft 6 (startpos): ~119M nodes, ~45M nps single-thread (magic bitboards)
- NNUE inference: ~18M evals/s (AVX2, incremental), ~0.9M full refresh
- Search: 6M nps midgame with TT+history (single thread)

---

## License

**AGPLv3** — see `LICENSE`. Commercial use without source disclosure requires a separate commercial license from the author (Vaibhav).

---

## Credits

- Yu Nasu (NNUE)
- Stockfish team for HalfKAv2_hm naming and SCReLU insights (independent impl)
- Lichess for Elite Database
