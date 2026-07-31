#!/usr/bin/env python3
"""Train a small perspective NNUE (768->256->32->1) on SF-labeled data."""
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
H1 = 256
H2 = 32


def decode_row(buf: bytes):
    pieces = buf[:64]
    stm = buf[64]
    (score,) = struct.unpack_from("<i", buf, 65)
    # Build feature vector from stm perspective
    feat = np.zeros(INPUT, dtype=np.float32)
    for sq, code in enumerate(pieces):
        if code == 0:
            continue
        if code <= 6:
            pt = code  # 1-6
            color_white = True
        else:
            pt = code - 8
            color_white = False
        # stm perspective: stm is "white"
        stm_white = stm == 1
        rel_white = color_white if stm_white else (not color_white)
        # flip square if stm is black
        s = sq if stm_white else (sq ^ 56)
        p = (0 if rel_white else 6) + (pt - 1)
        feat[p * 64 + s] = 1.0
    # target: score already from stm (SF pov(board.turn))
    target = np.float32(np.clip(score, -1500, 1500))
    return feat, target


class BinDataset(Dataset):
    ROW = 69  # 64 + 1 + 4

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
        x = torch.relu(self.fc0(x))
        x = torch.relu(self.fc1(x))
        return self.fc2(x)


def export_net(model: Net, path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        f.write(b"AHNNUEF1")
        f.write(struct.pack("<iii", INPUT, H1, H2))
        # row-major weights
        w0 = model.fc0.weight.detach().cpu().numpy().astype(np.float32)  # H1 x IN
        b0 = model.fc0.bias.detach().cpu().numpy().astype(np.float32)
        w1 = model.fc1.weight.detach().cpu().numpy().astype(np.float32)
        b1 = model.fc1.bias.detach().cpu().numpy().astype(np.float32)
        w2 = model.fc2.weight.detach().cpu().numpy().astype(np.float32).reshape(-1)
        b2 = model.fc2.bias.detach().cpu().numpy().astype(np.float32).reshape(-1)[0]
        for arr in (w0, b0, w1, b1, w2):
            f.write(arr.tobytes())
        f.write(struct.pack("<f", float(b2)))
    print(f"exported {path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", default=str(ROOT / "nets" / "default.nnue"))
    ap.add_argument("--epochs", type=int, default=8)
    ap.add_argument("--batch", type=int, default=512)
    ap.add_argument("--lr", type=float, default=1e-3)
    args = ap.parse_args()

    ds = BinDataset(args.data)
    print(f"positions: {len(ds)}")
    if len(ds) < 100:
        raise SystemExit("not enough data")
    n_val = max(100, len(ds) // 20)
    n_train = len(ds) - n_val
    train_ds, val_ds = torch.utils.data.random_split(ds, [n_train, n_val])
    train_loader = DataLoader(train_ds, batch_size=args.batch, shuffle=True, num_workers=0)
    val_loader = DataLoader(val_ds, batch_size=args.batch, shuffle=False)

    device = torch.device("cpu")
    model = Net().to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    loss_fn = nn.MSELoss()

    for epoch in range(1, args.epochs + 1):
        model.train()
        tr = 0.0
        n = 0
        for x, y in train_loader:
            x, y = x.to(device), y.to(device)
            pred = model(x)
            loss = loss_fn(pred, y)
            opt.zero_grad()
            loss.backward()
            opt.step()
            tr += loss.item() * x.size(0)
            n += x.size(0)
        model.eval()
        va = 0.0
        vn = 0
        with torch.no_grad():
            for x, y in val_loader:
                x, y = x.to(device), y.to(device)
                loss = loss_fn(model(x), y)
                va += loss.item() * x.size(0)
                vn += x.size(0)
        print(f"epoch {epoch}: train_mse={tr/n:.1f} val_mse={va/max(1,vn):.1f}", flush=True)

    export_net(model, Path(args.out))


if __name__ == "__main__":
    main()
