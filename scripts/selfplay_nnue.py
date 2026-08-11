#!/usr/bin/env python3
"""Classical vs NNUE self-play (same binary, env toggles)."""
from __future__ import annotations

import argparse
import os
import chess
import chess.engine
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADITYA = ROOT / "build" / "aditya"


def play_game(white, black, limit) -> float:
    """Return white's score: 1/0.5/0."""
    board = chess.Board()
    engines = {chess.WHITE: white, chess.BLACK: black}
    while not board.is_game_over(claim_draw=True):
        eng = engines[board.turn]
        try:
            mv = eng.play(board, limit).move
        except Exception:
            return 0.0 if board.turn == chess.WHITE else 1.0
        if mv is None or mv not in board.legal_moves:
            return 0.0 if board.turn == chess.WHITE else 1.0
        board.push(mv)
        if board.ply() > 400:
            break
    res = board.result(claim_draw=True)
    if res == "1-0":
        return 1.0
    if res == "0-1":
        return 0.0
    return 0.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=40)
    ap.add_argument("--movetime", type=float, default=0.1)
    ap.add_argument("--hash", type=int, default=64)
    ap.add_argument("--threads", type=int, default=1)
    ap.add_argument("--eval-file", default="nets/fast.nnue")
    ap.add_argument("--blend", type=int, default=0, help="ADITYA_NNUE_BLEND (0=pure NNUE)")
    args = ap.parse_args()

    env_classic = os.environ.copy()
    env_classic["ADITYA_USE_NNUE"] = "0"
    env_nnue = os.environ.copy()
    env_nnue["ADITYA_USE_NNUE"] = "1"
    env_nnue["ADITYA_NNUE_BLEND"] = str(args.blend)
    eval_path = str((ROOT / args.eval_file).resolve() if not Path(args.eval_file).is_absolute()
                    else Path(args.eval_file))
    env_nnue["ADITYA_NNUE"] = eval_path

    classic = chess.engine.SimpleEngine.popen_uci(str(ADITYA), env=env_classic)
    nnue = chess.engine.SimpleEngine.popen_uci(str(ADITYA), env=env_nnue)
    classic.configure({"Hash": args.hash, "Threads": args.threads})
    nnue.configure({"Hash": args.hash, "Threads": args.threads, "EvalFile": eval_path})
    print(f"NNUE file={eval_path} blend={args.blend}", flush=True)

    limit = chess.engine.Limit(time=args.movetime)
    points = 0.0  # NNUE score
    try:
        for g in range(args.games):
            nnue_white = (g % 2 == 0)
            if nnue_white:
                wscore = play_game(nnue, classic, limit)
                nscore = wscore
            else:
                wscore = play_game(classic, nnue, limit)
                nscore = 1.0 - wscore
            points += nscore
            print(
                f"game {g+1}/{args.games}: nnue={'W' if nnue_white else 'B'} "
                f"score={nscore} running={points/(g+1):.3f}",
                flush=True,
            )
        print(f"NNUE score {points}/{args.games} = {points/args.games:.3f} (blend={args.blend})")
    finally:
        classic.quit()
        nnue.quit()


if __name__ == "__main__":
    main()
