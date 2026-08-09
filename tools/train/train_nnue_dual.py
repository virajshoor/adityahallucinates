#!/usr/bin/env python3
"""Train dual-perspective NNUE (768->H1 shared, concat 2*H1->32->1).

Export magic AHNNUEF3. Features match C++ piece-square relative to a
fixed perspective (White/Black), then evaluate concatenates [us|them].
Supports H1=128 or 256 (C++ loader accepts both).
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader, ConcatDataset

ROOT = Path(__file__).resolve().parents[2]
INPUT = 768
H2 = 32


def features_for_perspective(pieces: bytes, persp_white: bool) -> np.ndarray:
    """Build 768 PS features from a fixed color perspective."""
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
        rel_white = color_white if persp_white else (not color_white)
        s = sq if persp_white else (sq ^ 56)
        p = (0 if rel_white else 6) + (pt - 1)
        feat[p * 64 + s] = 1.0
    return feat


def decode_row(buf: bytes):
    pieces = buf[:64]
    stm = buf[64]
    (score,) = struct.unpack_from("<i", buf, 65)
    stm_white = stm == 1
    us = features_for_perspective(pieces, stm_white)
    them = features_for_perspective(pieces, not stm_white)
    cp = float(np.clip(score, -1200, 1200))
    target = np.float32(cp / 100.0)
    return us, them, target


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
        us, them, target = decode_row(self.data[off : off + self.ROW])
        return (
            torch.from_numpy(us),
            torch.from_numpy(them),
            torch.tensor([target]),
        )


class DualNet(nn.Module):
    def __init__(self, h1: int = 128):
        super().__init__()
        self.h1 = h1
        self.fc0 = nn.Linear(INPUT, h1)  # shared across perspectives
        self.fc1 = nn.Linear(h1 * 2, H2)
        self.fc2 = nn.Linear(H2, 1)

    def forward(self, us, them):
        hus = torch.clamp(self.fc0(us), 0, 1)
        hthem = torch.clamp(self.fc0(them), 0, 1)
        x = torch.cat([hus, hthem], dim=1)
        x = torch.clamp(self.fc1(x), 0, 1)
        return self.fc2(x)


def export_f3(model: DualNet, path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    h1 = model.h1
    w0 = model.fc0.weight.detach().cpu().numpy().astype(np.float32)  # H1 x IN
    b0 = model.fc0.bias.detach().cpu().numpy().astype(np.float32)
    w1 = model.fc1.weight.detach().cpu().numpy().astype(np.float32)  # H2 x (2*H1)
    b1 = model.fc1.bias.detach().cpu().numpy().astype(np.float32)
    w2 = model.fc2.weight.detach().cpu().numpy().astype(np.float32).reshape(-1)
    b2 = float(model.fc2.bias.detach().cpu().numpy().reshape(-1)[0])
    w2_cp = w2 * 100.0
    b2_cp = b2 * 100.0
    with path.open("wb") as f:
        f.write(b"AHNNUEF3")
        f.write(struct.pack("<iii", INPUT, h1, H2))
        for arr in (w0, b0, w1, b1, w2_cp):
            f.write(arr.astype(np.float32).tobytes())
        f.write(struct.pack("<f", float(b2_cp)))
    print(f"exported {path} (AHNNUEF3 {INPUT}->{h1}*2->{H2})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", nargs="+", required=True, help="One or more .bin label files")
    ap.add_argument("--out", default=str(ROOT / "nets" / "fast.nnue"))
    ap.add_argument("--epochs", type=int, default=24)
    ap.add_argument("--batch", type=int, default=1024)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--h1", type=int, default=128, choices=[128, 256])
    args = ap.parse_args()

    datasets = [BinDataset(p) for p in args.data]
    ds: Dataset = datasets[0] if len(datasets) == 1 else ConcatDataset(datasets)
    print(f"positions: {len(ds)} from {len(datasets)} file(s); h1={args.h1}")
    n_val = max(500, len(ds) // 20)
    n_train = len(ds) - n_val
    train_ds, val_ds = torch.utils.data.random_split(ds, [n_train, n_val])
    train_loader = DataLoader(train_ds, batch_size=args.batch, shuffle=True, num_workers=0)
    val_loader = DataLoader(val_ds, batch_size=args.batch)

    model = DualNet(h1=args.h1)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    loss_fn = nn.MSELoss()
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs)

    for epoch in range(1, args.epochs + 1):
        model.train()
        tr = n = 0.0
        for us, them, y in train_loader:
            pred = model(us, them)
            loss = loss_fn(pred, y)
            opt.zero_grad()
            loss.backward()
            opt.step()
            tr += loss.item() * us.size(0)
            n += us.size(0)
        sched.step()
        model.eval()
        va = vn = 0.0
        with torch.no_grad():
            for us, them, y in val_loader:
                loss = loss_fn(model(us, them), y)
                va += loss.item() * us.size(0)
                vn += us.size(0)
        print(
            f"epoch {epoch}: train_mse={tr/n:.4f} val_mse={va/max(1,vn):.4f}",
            flush=True,
        )

    export_f3(model, Path(args.out))


if __name__ == "__main__":
    main()
