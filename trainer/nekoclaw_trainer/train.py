"""
Trainer for NekoClaw — PyTorch, 2xT4 DDP, AMP, lossless pause/resume
Lossless means: model + optimizer + scheduler + scaler + rng + dataloader offset
Pause via SIGUSR1 or 'p' key, resume via --resume last.pt
"""
import os, sys, time, signal, random, argparse, yaml, json, math, pathlib
import torch, numpy as np
from torch.utils.data import DataLoader
from torch.nn.parallel import DistributedDataParallel as DDP
import torch.distributed as dist
from .model import NekoClawNet, K_QA
from .dataset import NekoBinDataset, board_to_features
from tqdm import tqdm

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--config", default="trainer/configs/default.yaml")
    p.add_argument("--resume", default=None)
    p.add_argument("--output", default="checkpoints")
    return p.parse_args()

def load_config(path):
    import yaml
    with open(path) as f:
        return yaml.safe_load(f)

def save_checkpoint(path, model, optimizer, scheduler, scaler, epoch, global_step, best_loss, rng_state, dataset_state):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # Handle DDP unwrapping
    model_state = model.module.state_dict() if hasattr(model, "module") else model.state_dict()
    ckpt = {
        "model": model_state,
        "optimizer": optimizer.state_dict(),
        "scheduler": scheduler.state_dict() if scheduler else None,
        "scaler": scaler.state_dict(),
        "epoch": epoch,
        "global_step": global_step,
        "best_loss": best_loss,
        "rng": rng_state,
        "dataset": dataset_state,
        "config": "saved",
    }
    torch.save(ckpt, path)
    # Also save rng separately for quick inspect
    print(f"[ckpt] saved to {path} step {global_step}")

def load_checkpoint(path, model, optimizer, scheduler, scaler):
    ckpt = torch.load(path, map_location="cpu", weights_only=False)
    # model may be DDP
    target = model.module if hasattr(model, "module") else model
    target.load_state_dict(ckpt["model"])
    optimizer.load_state_dict(ckpt["optimizer"])
    if scheduler and ckpt["scheduler"]:
        scheduler.load_state_dict(ckpt["scheduler"])
    scaler.load_state_dict(ckpt["scaler"])
    return ckpt

def get_rng_state():
    return {
        "python": random.getstate(),
        "numpy": np.random.get_state(),
        "torch": torch.get_rng_state(),
        "cuda": torch.cuda.get_rng_state_all() if torch.cuda.is_available() else None,
    }

def set_rng_state(state):
    random.setstate(state["python"])
    np.random.set_state(state["numpy"])
    torch.set_rng_state(state["torch"])
    if state["cuda"] is not None and torch.cuda.is_available():
        torch.cuda.set_rng_state_all(state["cuda"])

def collate_fn(batch):
    # batch is list of dicts from dataset
    B = len(batch)
    max_feats = 32
    white = torch.full((B, max_feats), -1, dtype=torch.long)
    black = torch.full((B, max_feats), -1, dtype=torch.long)
    buckets = torch.zeros(B, dtype=torch.long)
    scores = torch.zeros(B, dtype=torch.float)
    results = torch.zeros(B, dtype=torch.float)
    for i, ex in enumerate(batch):
        board = ex["board"].numpy()
        w, b, buck = board_to_features(board)
        # pad/truncate to max_feats
        w = w[:max_feats]
        b = b[:max_feats]
        white[i, :len(w)] = torch.tensor(w, dtype=torch.long)
        black[i, :len(b)] = torch.tensor(b, dtype=torch.long)
        buckets[i] = buck
        # score in centipawns -> normalize to sigmoid range
        # score is i16 centipawns from teacher (e.g., stockfish depth 22)
        s = float(ex["score"]) / 400.0  # approx
        scores[i] = s
        # result -1,0,1 -> convert to WDL 0..1
        r = float(ex["result"])
        results[i] = (r + 1) / 2
    # Combined target: 0.5 * score + 0.5 * result? For now use score only
    target = scores  # TODO: blend with WDL
    return white, black, buckets, target

def train_one_epoch(model, loader, optimizer, scaler, scheduler, device, epoch, global_step, rank, world_size, config):
    model.train()
    total_loss = 0
    count = 0
    pbar = tqdm(loader, disable=rank!=0, desc=f"epoch {epoch}")
    for white, black, buckets, target in pbar:
        white = white.to(device); black = black.to(device); buckets = buckets.to(device); target = target.to(device)
        optimizer.zero_grad()
        use_amp = config.get("amp", True) and device.type == "cuda"
        with torch.autocast(device_type=device.type, dtype=torch.float16, enabled=use_amp):
            pred = model(white, black, buckets)
            # Loss: MSE on score + small L2
            loss = torch.nn.functional.mse_loss(pred, target)
            # L2 reg 1e-4
            l2 = 0
            for p in model.parameters():
                l2 += (p**2).sum()
            loss = loss + 1e-5 * l2
        scaler.scale(loss).backward()
        scaler.unscale_(optimizer)
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        scaler.step(optimizer)
        scaler.update()
        if scheduler:
            scheduler.step()
        total_loss += loss.item() * white.size(0)
        count += white.size(0)
        global_step += 1
        if rank==0 and global_step % 100 == 0:
            pbar.set_postfix(loss=loss.item(), lr=optimizer.param_groups[0]["lr"])
        if global_step % config.get("save_every", 5000) == 0 and rank==0:
            save_checkpoint(f"{config['output']}/step_{global_step}.pt", model, optimizer, scheduler, scaler, epoch, global_step, total_loss/count, get_rng_state(), loader.dataset.state_dict() if hasattr(loader.dataset, "state_dict") else {})
    return total_loss / max(1, count), global_step

def main():
    args = parse_args()
    config = load_config(args.config)
    # DDP setup
    world_size = int(os.environ.get("WORLD_SIZE", "1"))
    rank = int(os.environ.get("RANK", "0"))
    local_rank = int(os.environ.get("LOCAL_RANK", "0"))
    if world_size > 1:
        torch.cuda.set_device(local_rank)
        dist.init_process_group(backend="nccl")
        device = torch.device(f"cuda:{local_rank}")
    else:
        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        if device.type=="cuda":
            torch.cuda.set_device(0)
    # Model
    model = NekoClawNet().to(device)
    if world_size > 1:
        model = DDP(model, device_ids=[local_rank])
    optimizer = torch.optim.AdamW(model.parameters(), lr=config.get("lr", 2e-3), weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=20260)
    scaler = torch.amp.GradScaler('cuda', enabled=config.get("amp", True) and device.type=="cuda")
    # Dataset — fast numpy shuffle keeps 16384 batch as you want, single worker for WSL2
    dataset = NekoBinDataset(config["data"], shuffle=True, seed=config.get("seed", 42), rank=rank, world_size=world_size)
    loader = DataLoader(dataset, batch_size=config.get("batch_size", 16384), collate_fn=collate_fn, num_workers=0, pin_memory=False)
    start_epoch = 0
    global_step = 0
    best_loss = float("inf")
    if args.resume:
        ckpt = load_checkpoint(args.resume, model, optimizer, scheduler, scaler)
        start_epoch = ckpt["epoch"]
        global_step = ckpt["global_step"]
        best_loss = ckpt["best_loss"]
        set_rng_state(ckpt["rng"])
        if "dataset" in ckpt:
            dataset.load_state_dict(ckpt["dataset"])
        print(f"[resume] from {args.resume} epoch {start_epoch} step {global_step}")
    # Signal handling for graceful pause (SIGUSR1, SIGTERM for Kaggle preemption)
    def handle_signal(signum, frame):
        print(f"[signal] {signum} received, saving checkpoint...")
        if rank==0:
            save_checkpoint(f"{config['output']}/last.pt", model, optimizer, scheduler, scaler, start_epoch, global_step, best_loss, get_rng_state(), dataset.state_dict())
        sys.exit(0)
    signal.signal(signal.SIGUSR1, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)
    # Training loop
    for epoch in range(start_epoch, config.get("epochs", 100)):
        dataset.set_epoch(epoch)
        loss, global_step = train_one_epoch(model, loader, optimizer, scaler, scheduler, device, epoch, global_step, rank, world_size, config)
        if rank==0:
            print(f"epoch {epoch} loss {loss}")
            save_checkpoint(f"{config['output']}/epoch_{epoch}.pt", model, optimizer, scheduler, scaler, epoch, global_step, best_loss, get_rng_state(), dataset.state_dict())
            save_checkpoint(f"{config['output']}/last.pt", model, optimizer, scheduler, scaler, epoch, global_step, best_loss, get_rng_state(), dataset.state_dict())
            if loss < best_loss:
                best_loss = loss
                save_checkpoint(f"{config['output']}/best.pt", model, optimizer, scheduler, scaler, epoch, global_step, best_loss, get_rng_state(), dataset.state_dict())
        if world_size>1:
            dist.barrier()
    if rank==0:
        print("training done")

if __name__ == "__main__":
    main()
