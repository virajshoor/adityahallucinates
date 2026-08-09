#!/usr/bin/env python3
"""Train HalfKA dual-perspective NNUE (AHNNUEF4).

Features: 64 king buckets × 10 piece types (own/enemy PNBRQ) × 64 squares = 40960.
Shared FC0 over perspectives; concat [us|them] → 32 → 1.
Uses sparse feature lists (active indices only) for tractable training.
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
HALFKA_INPUT = 64 * 10 * 64  # 40960
H2 = 32
MAX_FEATS = 32  # <= 30 non-king pieces


def halfka_indices(pieces: bytes, persp_white: bool) -> list[int]:
    """Active HalfKA feature indices for one king perspective."""
    # Find king of this perspective
    king_code = 6 if persp_white else 14
    ksq_abs = None
    for sq, code in enumerate(pieces):
        if code == king_code:
            ksq_abs = sq
            break
    if ksq_abs is None:
        return []

    def rel(sq: int) -> int:
        return sq if persp_white else (sq ^ 56)

    ksq = rel(ksq_abs)
    feats: list[int] = []
    for sq, code in enumerate(pieces):
        if code == 0:
            continue
        if code <= 6:
            pt = code  # 1-6
            color_white = True
        else:
            pt = code - 8
            color_white = False
        if pt == 6:  # king
            continue
        own = color_white if persp_white else (not color_white)
        p = (0 if own else 5) + (pt - 1)  # 0-4 own PNBRQ, 5-9 enemy
        feats.append((ksq * 10 + p) * 64 + rel(sq))
    return feats


def decode_row(buf: bytes):
    pieces = buf[:64]
    stm = buf[64]
    (score,) = struct.unpack_from("<i", buf, 65)
    stm_white = stm == 1
    us = halfka_indices(pieces, stm_white)
    them = halfka_indices(pieces, not stm_white)
    target = np.float32(np.clip(score, -1200, 1200) / 100.0)
    return us, them, target


def pad_feats(feats: list[int]) -> tuple[np.ndarray, np.ndarray]:
    idx = np.zeros(MAX_FEATS, dtype=np.int64)
    mask = np.zeros(MAX_FEATS, dtype=np.float32)
    n = min(len(feats), MAX_FEATS)
    if n:
        idx[:n] = feats[:n]
        mask[:n] = 1.0
    return idx, mask


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
        us_i, us_m = pad_feats(us)
        th_i, th_m = pad_feats(them)
        return (
            torch.from_numpy(us_i),
            torch.from_numpy(us_m),
            torch.from_numpy(th_i),
            torch.from_numpy(th_m),
            torch.tensor([target]),
        )


class HalfKANet(nn.Module):
    def __init__(self, h1: int = 256):
        super().__init__()
        self.h1 = h1
        self.fc0 = nn.Embedding(HALFKA_INPUT, h1)  # sparse feature → h1 (no bias in emb)
        self.b0 = nn.Parameter(torch.zeros(h1))
        self.fc1 = nn.Linear(h1 * 2, H2)
        self.fc2 = nn.Linear(H2, 1)
        # Match Linear init scale roughly
        nn.init.uniform_(self.fc0.weight, -0.05, 0.05)

    def encode(self, idx, mask):
        # idx: [B, K], mask: [B, K]
        w = self.fc0(idx)  # [B, K, H]
        w = w * mask.unsqueeze(-1)
        h = w.sum(dim=1) + self.b0
        return torch.clamp(h, 0, 1)

    def forward(self, us_i, us_m, th_i, th_m):
        hus = self.encode(us_i, us_m)
        hthem = self.encode(th_i, th_m)
        x = torch.cat([hus, hthem], dim=1)
        x = torch.clamp(self.fc1(x), 0, 1)
        return self.fc2(x)


def export_f4(model: HalfKANet, path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    h1 = model.h1
    # Export as dense float AHNNUEF4 (C++ expects Linear layout HxIN)
    w0 = model.fc0.weight.detach().cpu().numpy().T.astype(np.float32)  # H1 x IN... wait emb is [IN, H]
    # Embedding weight: [num_embeddings, h1] = [IN, H1]
    # C++ expects wf0 as [h1][input] row-major (i * input + f)
    emb = model.fc0.weight.detach().cpu().numpy().astype(np.float32)  # IN x H1
    w0 = emb.T.copy()  # H1 x IN
    b0 = model.b0.detach().cpu().numpy().astype(np.float32)
    w1 = model.fc1.weight.detach().cpu().numpy().astype(np.float32)
    b1 = model.fc1.bias.detach().cpu().numpy().astype(np.float32)
    w2 = model.fc2.weight.detach().cpu().numpy().astype(np.float32).reshape(-1)
    b2 = float(model.fc2.bias.detach().cpu().numpy().reshape(-1)[0])
    w2_cp = w2 * 100.0
    b2_cp = b2 * 100.0
    with path.open("wb") as f:
        f.write(b"AHNNUEF4")
        f.write(struct.pack("<iii", HALFKA_INPUT, h1, H2))
        for arr in (w0, b0, w1, b1, w2_cp):
            f.write(arr.astype(np.float32).tobytes())
        f.write(struct.pack("<f", float(b2_cp)))
    print(f"exported {path} (AHNNUEF4 {HALFKA_INPUT}->{h1}*2->{H2})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", nargs="+", required=True)
    ap.add_argument("--out", default=str(ROOT / "nets" / "halfka.nnue"))
    ap.add_argument("--epochs", type=int, default=16)
    ap.add_argument("--batch", type=int, default=1024)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--h1", type=int, default=256, choices=[128, 256])
    args = ap.parse_args()

    datasets = [BinDataset(p) for p in args.data]
    ds: Dataset = datasets[0] if len(datasets) == 1 else ConcatDataset(datasets)
    print(f"positions: {len(ds)} files={len(datasets)} h1={args.h1}")
    n_val = max(1000, len(ds) // 20)
    n_train = len(ds) - n_val
    train_ds, val_ds = torch.utils.data.random_split(ds, [n_train, n_val])
    train_loader = DataLoader(train_ds, batch_size=args.batch, shuffle=True, num_workers=0)
    val_loader = DataLoader(val_ds, batch_size=args.batch)

    model = HalfKANet(h1=args.h1)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    loss_fn = nn.MSELoss()
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs)

    for epoch in range(1, args.epochs + 1):
        model.train()
        tr = n = 0.0
        for us_i, us_m, th_i, th_m, y in train_loader:
            pred = model(us_i, us_m, th_i, th_m)
            loss = loss_fn(pred, y)
            opt.zero_grad()
            loss.backward()
            opt.step()
            tr += loss.item() * us_i.size(0)
            n += us_i.size(0)
        sched.step()
        model.eval()
        va = vn = 0.0
        with torch.no_grad():
            for us_i, us_m, th_i, th_m, y in val_loader:
                loss = loss_fn(model(us_i, us_m, th_i, th_m), y)
                va += loss.item() * us_i.size(0)
                vn += us_i.size(0)
        print(
            f"epoch {epoch}: train_mse={tr/n:.4f} val_mse={va/max(1,vn):.4f}",
            flush=True,
        )

    export_f4(model, Path(args.out))


if __name__ == "__main__":
    main()
