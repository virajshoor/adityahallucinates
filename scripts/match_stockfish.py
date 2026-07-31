#!/usr/bin/env python3
"""Play matches between Aditya and Stockfish at a given skill/Elo.

Target: score >= 75% against Stockfish Skill Level (default 5),
or against UCI_Elo-limited Stockfish.

Usage:
  python3 scripts/match_stockfish.py [--games N] [--skill S] [--elo E] [--movetime MS]
"""
from __future__ import annotations

import argparse
import chess
import chess.engine
import chess.pgn
import datetime as dt
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ADITYA = ROOT / "build" / "aditya"
STOCKFISH = ROOT / "third_party" / "stockfish" / "stockfish-ubuntu-x86-64-avx2"
RESULTS = ROOT / "results"


def play_game(aditya, sf, aditya_white: bool, movetime: float, board: chess.Board | None = None):
    board = board or chess.Board()
    game = chess.pgn.Game()
    game.headers["White"] = "Aditya" if aditya_white else "Stockfish"
    game.headers["Black"] = "Stockfish" if aditya_white else "Aditya"
    node = game
    limit = chess.engine.Limit(time=movetime)

    while not board.is_game_over(claim_draw=True):
        engine = aditya if (board.turn == chess.WHITE) == aditya_white else sf
        try:
            result = engine.play(board, limit)
        except Exception as e:
            print(f"engine error: {e}", file=sys.stderr)
            break
        if result.move is None:
            break
        board.push(result.move)
        node = node.add_variation(result.move)

    outcome = board.outcome(claim_draw=True)
    if outcome is None:
        # unfinished
        return 0.5, game, "unfinished"
    if outcome.winner is None:
        score_white = 0.5
        term = outcome.termination.name
    elif outcome.winner == chess.WHITE:
        score_white = 1.0
        term = outcome.termination.name
    else:
        score_white = 0.0
        term = outcome.termination.name

    aditya_score = score_white if aditya_white else 1.0 - score_white
    game.headers["Result"] = board.result(claim_draw=True)
    game.headers["Termination"] = term
    return aditya_score, game, term


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=20)
    ap.add_argument("--skill", type=int, default=5, help="Stockfish Skill Level 0-20")
    ap.add_argument("--elo", type=int, default=0, help="If >0, use UCI_LimitStrength + UCI_Elo")
    ap.add_argument("--movetime", type=float, default=0.1, help="seconds per move")
    ap.add_argument("--hash", type=int, default=64)
    ap.add_argument("--target", type=float, default=0.75, help="target score fraction")
    args = ap.parse_args()

    if not ADITYA.exists():
        sys.exit(f"missing engine binary: {ADITYA}")
    if not STOCKFISH.exists():
        sys.exit(f"missing stockfish: {STOCKFISH}")

    RESULTS.mkdir(exist_ok=True)

    aditya = chess.engine.SimpleEngine.popen_uci(str(ADITYA))
    sf = chess.engine.SimpleEngine.popen_uci(str(STOCKFISH))

    try:
        aditya.configure({"Hash": args.hash})
        sf.configure({"Hash": args.hash, "Threads": 1})
        if args.elo > 0:
            sf.configure({"UCI_LimitStrength": True, "UCI_Elo": args.elo})
            label = f"elo{args.elo}"
        else:
            sf.configure({"Skill Level": args.skill})
            label = f"skill{args.skill}"

        scores = []
        games_path = RESULTS / f"match_{label}_{dt.datetime.utcnow().strftime('%Y%m%d_%H%M%S')}.pgn"
        summary = {"label": label, "movetime": args.movetime, "games_results": []}

        with games_path.open("w") as pgn_out:
            for i in range(args.games):
                aditya_white = (i % 2 == 0)
                score, game, term = play_game(aditya, sf, aditya_white, args.movetime)
                scores.append(score)
                summary["game_results"].append({
                    "game": i + 1,
                    "aditya_white": aditya_white,
                    "score": score,
                    "termination": term,
                    "result": game.headers.get("Result"),
                })
                print(game, file=pgn_out, end="\n\n")
                avg = sum(scores) / len(scores)
                print(
                    f"game {i+1}/{args.games}: aditya={'W' if aditya_white else 'B'} "
                    f"score={score} term={term} running={avg:.3f}",
                    flush=True,
                )

        total = sum(scores)
        avg = total / len(scores)
        summary["points"] = total
        summary["games"] = len(scores)
        summary["score"] = avg
        summary["target"] = args.target
        summary["passed"] = avg >= args.target
        out_json = RESULTS / f"summary_{label}.json"
        out_json.write_text(json.dumps(summary, indent=2))
        print(json.dumps(summary, indent=2))
        print(f"PGN: {games_path}")
        return 0 if summary["passed"] else 2
    finally:
        aditya.quit()
        sf.quit()


if __name__ == "__main__":
    sys.exit(main())
