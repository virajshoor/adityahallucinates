#!/usr/bin/env python3
"""Label positions with our classical search score (distillation targets)."""
from __future__ import annotations
import argparse, chess, chess.engine, random, struct, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
ADITYA = ROOT / "build" / "aditya"
STOCKFISH = ROOT / "third_party" / "stockfish" / "stockfish-ubuntu-x86-64-avx2"
OUT = ROOT / "tools" / "datagen" / "output"

def pack(board, score_cp: int) -> bytes:
    pieces = bytearray(64)
    for sq in chess.SQUARES:
        p = board.piece_at(sq)
        if p is None: pieces[sq] = 0
        else:
            code = p.piece_type
            if p.color == chess.BLACK: code += 8
            pieces[sq] = code
    stm = 1 if board.turn == chess.WHITE else 0
    return bytes(pieces) + bytes([stm]) + struct.pack("<i", int(score_cp))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=500)
    ap.add_argument("--depth", type=int, default=8)
    ap.add_argument("--sf-move-depth", type=int, default=6)
    ap.add_argument("--out", default=str(OUT / "distill_v1.bin"))
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    eng = chess.engine.SimpleEngine.popen_uci(str(ADITYA))
    sf = chess.engine.SimpleEngine.popen_uci(str(STOCKFISH))
    eng.configure({"Hash": 128, "Threads": 1})
    sf.configure({"Hash": 128, "Threads": 1})
    rows = []
    try:
        for g in range(args.games):
            board = chess.Board()
            n = 0
            for ply in range(180):
                if board.is_game_over(claim_draw=True): break
                if ply >= 4:
                    info = eng.analyse(board, chess.engine.Limit(depth=args.depth))
                    sc = info["score"].pov(board.turn).score(mate_score=30000)
                    if sc is not None and abs(sc) < 15000:
                        rows.append(pack(board, sc)); n += 1
                # Mix SF + random moves for diversity
                mv = None
                if random.random() < 0.85:
                    try: mv = sf.play(board, chess.engine.Limit(depth=args.sf_move_depth)).move
                    except Exception: mv = None
                if mv is None:
                    legal = list(board.legal_moves)
                    if not legal: break
                    mv = random.choice(legal)
                board.push(mv)
            print(f"game {g+1}/{args.games}: +{n} total={len(rows)}", flush=True)
    finally:
        eng.quit(); sf.quit()
    Path(args.out).write_bytes(b"".join(rows))
    print(f"wrote {len(rows)} -> {args.out}")
if __name__ == "__main__":
    main()
