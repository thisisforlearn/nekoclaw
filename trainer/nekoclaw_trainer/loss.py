import torch
def wdl_loss(pred, target, result):
    # pred: centipawns, target: score, result: -1,0,1
    # Use MSE for score + cross entropy for WDL head if available
    return torch.nn.functional.mse_loss(pred, target)
