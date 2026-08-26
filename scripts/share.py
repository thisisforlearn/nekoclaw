#!/usr/bin/env python3
"""
Share trained NNUE online with Elo estimate
"""
import pathlib, subprocess, json, sys
try:
    from rich.console import Console
    console=Console()
except: console=None

def estimate_elo(data_mb, loss):
    # Rough: 50MB~1550, 500MB~1900, 5GB~2300, plus loss
    base=1500+ (data_mb/10)
    if loss<0.5: base+=200
    elif loss<1: base+=100
    return int(min(3000, base))

def main():
    import argparse
    p=argparse.ArgumentParser()
    p.add_argument("--ckpt", default="checkpoints/best.pt")
    p.add_argument("--data-mb", type=int, default=500)
    p.add_argument("--loss", type=float, default=0.5)
    args=p.parse_args()
    elo=estimate_elo(args.data_mb, args.loss)
    print(f"Elo estimate: {elo} for {args.data_mb}MB loss {args.loss}")
    # Export if needed
    nnue=f"weights/nekoclaw-{elo}elo.nnue"
    if not pathlib.Path(nnue).exists():
        subprocess.run(["python","-m","nekoclaw_trainer.export","--ckpt",args.ckpt,"--out",nnue], check=False)
    print(f"NNUE at {nnue} ({pathlib.Path(nnue).stat().st_size/1024/1024:.1f}MB)")
    # Push to GitHub
    if input(f"Push {nnue} to GitHub with Elo {elo}? [Y/n] ").lower() not in ["n","no"]:
        subprocess.run(["git","add",nnue], check=False)
        subprocess.run(["git","commit","-m",f"NNUE {elo} Elo - {args.data_mb}MB"], check=False)
        subprocess.run(["git","push","origin","main"], check=False)
        print(f"Pushed to https://github.com/thisisforlearn/nekoclaw/tree/main/weights")
    # Also share to HuggingFace?
    print("Share to HuggingFace: huggingface-cli upload thisisforlearn/nekoclaw-weights "+nnue)

if __name__=="__main__":
    main()
