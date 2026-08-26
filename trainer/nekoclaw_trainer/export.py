"""
Export PyTorch checkpoint to C++ .nnue binary (little-endian, versioned)
"""
import torch, struct, argparse, pathlib
from .model import NekoClawNet

MAGIC = 0x4E434C57  # NCLW
VERSION = 1
ARCH = 0x10240408

def export(ckpt_path, out_path):
    ckpt = torch.load(ckpt_path, map_location="cpu", weights_only=False)
    state = ckpt["model"] if "model" in ckpt else ckpt
    net = NekoClawNet()
    # handle DDP prefix
    new_state = {}
    for k,v in state.items():
        nk = k.replace("module.", "")
        new_state[nk] = v
    net.load_state_dict(new_state)
    q = net.export_quantized()
    with open(out_path, "wb") as f:
        f.write(struct.pack("<III", MAGIC, VERSION, ARCH))
        # FT
        f.write(q["ft_w"].numpy().tobytes())
        f.write(q["ft_b"].numpy().tobytes())
        for b in range(8):
            # L1
            f.write(q["l1_w"][b].numpy().tobytes())
            f.write(q["l1_b"][b].numpy().tobytes())
            f.write(q["l2_w"][b].numpy().tobytes())
            f.write(q["l2_b"][b].numpy().tobytes())
            f.write(q["l3_w"][b].numpy().tobytes())
            f.write(q["l3_b"][b].numpy().tobytes())
            f.write(q["out_w"][b].numpy().tobytes())
            f.write(struct.pack("<i", int(q["out_b"][b].item())))
    print(f"exported to {out_path} from {ckpt_path}")

if __name__ == "__main__":
    import argparse
    p=argparse.ArgumentParser()
    p.add_argument("--ckpt", required=True)
    p.add_argument("--out", required=True)
    args=p.parse_args()
    export(args.ckpt, args.out)
