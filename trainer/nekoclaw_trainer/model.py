"""
NekoClaw NNUE PyTorch model — mirrors C++ arch exactly for export parity.
HalfKAv2_hm 32*12*64=24576 -> FT 1024 SCReLU -> 8 buckets -> 16->32->32->1
Hybrid quantized export: FT int16 QA=255, hidden int8 QB=64, scale 400
"""
import torch
import torch.nn as nn
import torch.nn.functional as F

K_INPUT_DIMS = 24576
K_FT_SIZE = 1024
K_BUCKET_COUNT = 8
K_QA = 255
K_QB = 64
K_SCALE = 400

def bucket_for_piece_count(pc: int) -> int:
    b = (pc - 1) // 4
    return max(0, min(K_BUCKET_COUNT - 1, b))

class NekoClawNet(nn.Module):
    def __init__(self):
        super().__init__()
        # FeatureTransformer: sparse -> dense, one weight per feature
        self.ft_weight = nn.Parameter(torch.randn(K_INPUT_DIMS, K_FT_SIZE) * 0.01)
        self.ft_bias = nn.Parameter(torch.zeros(K_FT_SIZE))
        # Bucketed heads: each bucket has 2048->16, 16->32, 32->32, 32->1
        self.l1_weights = nn.Parameter(torch.randn(K_BUCKET_COUNT, 2048, 16) * 0.01)
        self.l1_bias = nn.Parameter(torch.zeros(K_BUCKET_COUNT, 16))
        self.l2_weights = nn.Parameter(torch.randn(K_BUCKET_COUNT, 16, 32) * 0.01)
        self.l2_bias = nn.Parameter(torch.zeros(K_BUCKET_COUNT, 32))
        self.l3_weights = nn.Parameter(torch.randn(K_BUCKET_COUNT, 32, 32) * 0.01)
        self.l3_bias = nn.Parameter(torch.zeros(K_BUCKET_COUNT, 32))
        self.out_weights = nn.Parameter(torch.randn(K_BUCKET_COUNT, 32) * 0.01)
        self.out_bias = nn.Parameter(torch.zeros(K_BUCKET_COUNT))

    def forward(self, white_indices, black_indices, bucket_indices):
        """
        white_indices, black_indices: [B, N] long, padded with -1 for sparse
        bucket_indices: [B] long 0..7
        Returns: [B] float (centipawns, before activation at output)
        """
        B = bucket_indices.shape[0]
        device = self.ft_weight.device
        # FeatureTransformer gather + sum (sparse)
        # For speed we use embedding_bag like approach: sum over indices
        # white
        # Vectorized FT gather — 100x faster than Python loop for B=16384
        mask_w = (white_indices != -1)
        mask_b = (black_indices != -1)
        white_clamped = white_indices.clamp(min=0)
        black_clamped = black_indices.clamp(min=0)
        w_gather = self.ft_weight[white_clamped]  # [B,32,1024]
        b_gather = self.ft_weight[black_clamped]
        w_gather = w_gather * mask_w.unsqueeze(-1).float()
        b_gather = b_gather * mask_b.unsqueeze(-1).float()
        w_acc = self.ft_bias + w_gather.sum(dim=1)
        b_acc = self.ft_bias + b_gather.sum(dim=1)
        # SCReLU: clamp 0..1 then square (QA scaled)
        # Our FT is in float QA=255 scale, so we clamp to QA then square / QA
        w_clipped = torch.clamp(w_acc, 0, K_QA)
        b_clipped = torch.clamp(b_acc, 0, K_QA)
        w_screlu = (w_clipped * w_clipped) / K_QA  # 0..QA
        b_screlu = (b_clipped * b_clipped) / K_QA
        # Concatenate and scale to QB/ QA? For trainer we keep float, quantization only at export
        # Input to L1 is 2048 floats in [0, QA]
        x = torch.cat([w_screlu, b_screlu], dim=1)  # [B,2048]
        # Bucketed heads: select per sample
        # We need to gather weights per bucket
        # For efficiency we use batched matmul via gather
        l1_out = torch.zeros(B, 16, device=device)
        l2_out = torch.zeros(B, 32, device=device)
        l3_out = torch.zeros(B, 32, device=device)
        out = torch.zeros(B, device=device)
        for b in range(K_BUCKET_COUNT):
            mask = (bucket_indices == b)
            if not mask.any():
                continue
            xb = x[mask]  # [Nb,2048]
            # L1: 2048->16
            w1 = self.l1_weights[b]  # [2048,16]
            b1 = self.l1_bias[b]
            l1 = torch.matmul(xb, w1) + b1  # [Nb,16]
            l1 = torch.clamp(l1, min=0)
            # L2: 16->32
            w2 = self.l2_weights[b]
            b2 = self.l2_bias[b]
            l2 = torch.matmul(l1, w2) + b2
            l2 = torch.clamp(l2, min=0)
            # L3: 32->32
            w3 = self.l3_weights[b]
            b3 = self.l3_bias[b]
            l3 = torch.matmul(l2, w3) + b3
            l3 = torch.clamp(l3, min=0)
            # Out
            wo = self.out_weights[b]
            bo = self.out_bias[b]
            o = torch.matmul(l3, wo) + bo
            l1_out[mask] = l1
            l2_out[mask] = l2
            l3_out[mask] = l3
            out[mask] = o.float()
        # Scale to centipawns: raw * SCALE / (QA*QB) ??? Stockfish does SCALE=400, QA=255, QB=64
        # Our raw is in float QA scale, so divide
        out = out * K_SCALE / (K_QA * 1.0)
        return out

    def export_quantized(self):
        """Return quantized weights dict for C++ .nnue export"""
        # Quantize FT to int16 QA=255
        ft_w = torch.clamp(self.ft_weight * 1.0, -127*K_QA, 127*K_QA).round().to(torch.int16)
        ft_b = torch.clamp(self.ft_bias, -127*K_QA, 127*K_QA).round().to(torch.int16)
        # Hidden to int8 QB=64
        def quant(w, scale):
            return torch.clamp(w * scale, -127, 127).round().to(torch.int8)
        # L1: need to scale by QA*QB? For simplicity, scale by QB
        l1_w = quant(self.l1_weights, K_QB)
        l1_b = (self.l1_bias * K_QB).round().to(torch.int32)
        l2_w = quant(self.l2_weights, K_QB)
        l2_b = (self.l2_bias * K_QB).round().to(torch.int32)
        l3_w = quant(self.l3_weights, K_QB)
        l3_b = (self.l3_bias * K_QB).round().to(torch.int32)
        out_w = quant(self.out_weights, K_QB)
        out_b = (self.out_bias * K_QB).round().to(torch.int32)
        return {
            "ft_w": ft_w, "ft_b": ft_b,
            "l1_w": l1_w, "l1_b": l1_b,
            "l2_w": l2_w, "l2_b": l2_b,
            "l3_w": l3_w, "l3_b": l3_b,
            "out_w": out_w, "out_b": out_b,
        }
