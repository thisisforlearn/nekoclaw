#!/usr/bin/env python3
"""
Nerd settings — shows everything in detail, auto-fixes issues
"""
import pathlib, subprocess, json, os, sys
try:
    from rich.console import Console
    from rich.panel import Panel
    console=Console()
except: console=None

def check():
    issues=[]
    if not pathlib.Path("build/nekoclaw").exists():
        issues.append(("Missing build/nekoclaw", "cmake -S engine -B build && cmake --build build -j"))
    if not pathlib.Path("data/train.bin").exists():
        issues.append(("No data/train.bin", "python scripts/train_ui.py --size 500"))
    if not pathlib.Path("weights/nekoclaw-1024x8-scReLU.nnue").exists():
        issues.append(("No pre-trained NNUE", "curl -L https://github.com/thisisforlearn/nekoclaw/releases/download/v1.0.0/nekoclaw-1024x8-scReLU.nnue -o weights/nekoclaw-1024x8-scReLU.nnue"))
    # Check perft
    try:
        out=subprocess.check_output(["./build/perft"], text=True, timeout=5)
        if "FAIL" in out: issues.append(("Perft FAIL", "./build/perft"))
    except: issues.append(("perft not runnable", "cargo build"))
    return issues

def main():
    if console: console.print(Panel.fit("[bold red]Nerd Settings — Auto-Fix[/]", border_style="red"))
    issues=check()
    if not issues:
        print("✔ All good — no issues")
    else:
        for i,(desc,fix) in enumerate(issues,1):
            print(f"{i}. {desc} -> fix: {fix}")
            if input(f"Fix {desc}? [Y/n] ").lower() not in ["n","no"]:
                os.system(fix)
                print("Fixed")
    # Show all settings
    print("\nAll settings:")
    for p in ["engine/CMakeLists.txt","trainer/configs/default.yaml","tui/Cargo.toml"]:
        print(f" - {p}: {pathlib.Path(p).stat().st_size} bytes" if pathlib.Path(p).exists() else f" - {p} missing")

if __name__=="__main__":
    main()
