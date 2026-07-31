#!/usr/bin/env python3
"""Generate SF-labeled training positions via self-play / random walks."""
from __future__ import annotations

import argparse
import chess
import chess.engine
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STOCKFISH = ROOT / "third_party" / "stockfish" / "stockfish-ubuntu-x86-64-avx2"
ADITYA = ROOT / "build" / "aditya"
OUT_DIR = ROOT / "tools" / "datagen" / "output"


def pack_position(board: chess.Board, score_cp: int) -> bytes:
    """Pack: 64 bytes piece placement + 1 stm + 4 score int32 LE.
    Piece codes: 0 empty, 1-6 white PNBRQK, 9-14 black pnbrqk
    """
    pieces = bytearray(64)
    for sq in chess.SQUARES:
        p = board.piece_at(sq)
        if p is None:
            pieces[sq] = 0
        else:
            code = p.piece_type  # 1-6
            if p.color == chess.BLACK:
                code += 8
            pieces[sq] = code
    stm = 1 if board.turn == chess.WHITE else 0
    return bytes(pieces) + bytes([stm]) + struct.pack("<i", int(score_cp))


def play_game(aditya, sf, label_limit, max_plies=160):
    board = chess.Board()
    rows = []
    # Mix of engine moves and random for diversity
    for ply in range(max_plies):
        if board.is_game_over(claim_draw=True):
            break
        # Label current position with Stockfish (from stm)
        try:
            info = sf.analyse(board, label_limit)
            pov = info["score"].pov(board.turn)
            if pov.is_mate():
                mate = pov.mate()
                cp = 30000 - abs(mate) * 10 if mate else 0
                cp = cp if mate > 0 else -cp
            else:
                cp = pov.score(mate_score=30000)
                if cp is None:
                    cp = 0
        except Exception:
            cp = 0

        # Skip very early opening / huge mates for training stability
        if ply >= 4 and abs(cp) < 15000:
            rows.append(pack_position(board, cp))

        # Move selection
        if random.random() < 0.7 and aditya is not None:
            try:
                mv = aditya.play(board, chess.engine.Limit(time=0.01)).move
            except Exception:
                mv = None
        else:
            mv = None
        if mv is None:
            legal = list(board.legal_moves)
            if not legal:
                break
            # Prefer captures sometimes
            caps = [m for m in legal if board.is_capture(m)]
            mv = random.choice(caps) if caps and random.random() < 0.3 else random.choice(legal)
        board.push(mv)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=200)
    ap.add_argument("--out", type=str, default=str(OUT_DIR / "sf_labels.bin"))
    ap.add_argument("--depth", type=int, default=6)
    ap.add_argument("--nodes", type=int, default=0)
    ap.add_argument("--use-aditya", action="store_true")
    args = ap.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    sf = chess.engine.SimpleEngine.popen_uci(str(STOCKFISH))
    sf.configure({"Hash": 64, "Threads": 1})
    aditya = None
    if args.use_aditya and ADITYA.exists():
        aditya = chess.engine.SimpleEngine.popen_uci(str(ADITYA))
        aditya.configure({"Hash": 32})

    limit = chess.engine.Limit(depth=args.depth) if args.nodes <= 0 else chess.engine.Limit(nodes=args.nodes)
    total = 0
    try:
        with open(args.out, "wb") as out:
            for g in range(args.games):
                rows = play_game(aditya, sf, limit)
                for r in rows:
                    out.write(r)
                total += len(rows)
                print(f"game {g+1}/{args.games}: +{len(rows)} positions total={total}", flush=True)
        print(f"wrote {total} positions -> {args.out}")
    finally:
        sf.quit()
        if aditya:
            aditya.quit()


if __name__ == "__main__":
    main()
