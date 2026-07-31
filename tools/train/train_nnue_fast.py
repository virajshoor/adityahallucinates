#!/usr/bin/env python3
"""Train a small int16-friendly NNUE (768->128->32->1) on SF-labeled data."""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader

ROOT = Path(__file__).resolve().parents[2]
INPUT = 768
H1 = 128
H2 = 32


def decode_row(buf: bytes):
    pieces = buf[:64]
    stm = buf[64]
    (score,) = struct.unpack_from("<i", buf, 65)
    feat = np.zeros(INPUT, dtype=np.float32)
    for sq, code in enumerate(pieces):
        if code == 0:
            continue
        if code <= 6:
            pt = code
            color_white = True
        else:
            pt = code - 8
            color_white = False
        stm_white = stm == 1
        rel_white = color_white if stm_white else (not color_white)
        s = sq if stm_white else (sq ^ 56)
        p = (0 if rel_white else 6) + (pt - 1)
        feat[p * 64 + s] = 1.0
    target = np.float32(np.clip(score, -1200, 1200) / 100.0)  # train in pawns
    return feat, target


class BinDataset(Dataset):
    ROW = 69

    def __init__(self, path: str):
        data = Path(path).read_bytes()
        self.n = len(data) // self.ROW
        self.data = data

    def __len__(self):
        return self.n

    def __getitem__(self, idx):
        off = idx * self.ROW
        feat, target = decode_row(self.data[off : off + self.ROW])
        return torch.from_numpy(feat), torch.tensor([target])


class Net(nn.Module):
    def __init__(self):
        super().__init__()
        self.fc0 = nn.Linear(INPUT, H1)
        self.fc1 = nn.Linear(H1, H2)
        self.fc2 = nn.Linear(H2, 1)

    def forward(self, x):
        x = torch.clamp(self.fc0(x), 0, 1)  # clipped ReLU like SF
        x = torch.clamp(self.fc1(x), 0, 1)
        return self.fc2(x)


def quantize_export(model: Net, path: Path):
    """Export AHNNUEI1: int16 weights + scales for fast inference; output in cp."""
    path.parent.mkdir(parents=True, exist_ok=True)
    w0 = model.fc0.weight.detach().cpu().numpy().astype(np.float32)  # H1 x IN
    b0 = model.fc0.bias.detach().cpu().numpy().astype(np.float32)
    w1 = model.fc1.weight.detach().cpu().numpy().astype(np.float32)
    b1 = model.fc1.bias.detach().cpu().numpy().astype(np.float32)
    w2 = model.fc2.weight.detach().cpu().numpy().astype(np.float32).reshape(-1)
    b2 = float(model.fc2.bias.detach().cpu().numpy().reshape(-1)[0])

    def q16(arr, max_abs=None):
        m = float(np.max(np.abs(arr))) if max_abs is None else max_abs
        m = max(m, 1e-8)
        scale = 32767.0 / m
        q = np.clip(np.round(arr * scale), -32767, 32767).astype(np.int16)
        return q, scale

    # Keep float export path for AHNNUEF2 (128-wide) as well for simplicity of C++ loader
    with path.open("wb") as f:
        f.write(b"AHNNUEF2")
        f.write(struct.pack("<iii", INPUT, H1, H2))
        # Store float weights but output already in pawns * 100 for cp
        # Scale final layer so network outputs centipawns
        w2_cp = w2 * 100.0
        b2_cp = b2 * 100.0
        for arr in (w0, b0, w1, b1, w2_cp):
            f.write(arr.astype(np.float32).tobytes())
        f.write(struct.pack("<f", float(b2_cp)))
    print(f"exported {path} (AHNNUEF2 {INPUT}->{H1}->{H2})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", default=str(ROOT / "nets" / "fast.nnue"))
    ap.add_argument("--epochs", type=int, default=12)
    ap.add_argument("--batch", type=int, default=1024)
    ap.add_argument("--lr", type=float, default=1e-3)
    args = ap.parse_args()

    ds = BinDataset(args.data)
    print(f"positions: {len(ds)}")
    n_val = max(200, len(ds) // 20)
    n_train = len(ds) - n_val
    train_ds, val_ds = torch.utils.data.random_split(ds, [n_train, n_val])
    train_loader = DataLoader(train_ds, batch_size=args.batch, shuffle=True, num_workers=0)
    val_loader = DataLoader(val_ds, batch_size=args.batch)

    model = Net()
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    loss_fn = nn.MSELoss()
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs)

    for epoch in range(1, args.epochs + 1):
        model.train()
        tr = n = 0.0
        for x, y in train_loader:
            pred = model(x)
            loss = loss_fn(pred, y)
            opt.zero_grad()
            loss.backward()
            opt.step()
            tr += loss.item() * x.size(0)
            n += x.size(0)
        sched.step()
        model.eval()
        va = vn = 0.0
        with torch.no_grad():
            for x, y in val_loader:
                loss = loss_fn(model(x), y)
                va += loss.item() * x.size(0)
                vn += x.size(0)
        print(f"epoch {epoch}: train_mse={tr/n:.4f} val_mse={va/max(1,vn):.4f}", flush=True)

    quantize_export(model, Path(args.out))


if __name__ == "__main__":
    main()
