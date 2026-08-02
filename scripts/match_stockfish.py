#!/usr/bin/env python3
"""Match Aditya vs Stockfish; target score >= 75% at a given skill/Elo."""
from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

import chess
import chess.engine
import chess.pgn

ROOT = Path(__file__).resolve().parents[1]
ADITYA = ROOT / "build" / "aditya"
STOCKFISH = ROOT / "third_party" / "stockfish" / "stockfish-ubuntu-x86-64-avx2"
RESULTS = ROOT / "results"


def newgame(engine: chess.engine.SimpleEngine) -> None:
    try:
        engine.protocol.send_line("ucinewgame")
        engine.ping()
    except Exception:
        pass


def play_game(aditya, sf, aditya_white: bool, movetime: float):
    newgame(aditya)
    newgame(sf)

    board = chess.Board()
    game = chess.pgn.Game()
    game.headers["White"] = "Aditya" if aditya_white else "Stockfish"
    game.headers["Black"] = "Stockfish" if aditya_white else "Aditya"
    game.headers["Date"] = datetime.now(timezone.utc).strftime("%Y.%m.%d")
    node = game
    limit = chess.engine.Limit(time=movetime)
    # Hard wall-clock per move: engines must not hang the match
    move_wall = max(movetime * 4.0, movetime + 2.0)

    while not board.is_game_over(claim_draw=True):
        engine = aditya if ((board.turn == chess.WHITE) == aditya_white) else sf
        try:
            result = engine.play(board, limit, timeout=move_wall)
        except chess.engine.TimeoutError:
            try:
                engine.protocol.send_line("stop")
                engine.ping()
            except Exception:
                pass
            # Treat timeout as resign for the side to move
            if board.turn == chess.WHITE:
                game.headers["Result"] = "0-1"
            else:
                game.headers["Result"] = "1-0"
            game.headers["Termination"] = "TIME_FORFEIT"
            aditya_to_move = (board.turn == chess.WHITE) == aditya_white
            return (0.0 if aditya_to_move else 1.0), game, "TIME_FORFEIT"
        if result.move is None:
            break
        board.push(result.move)
        node = node.add_variation(result.move)

    outcome = board.outcome(claim_draw=True)
    if outcome is None:
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

    aditya_score = score_white if aditya_white else (1.0 - score_white)
    game.headers["Result"] = board.result(claim_draw=True)
    game.headers["Termination"] = term
    return aditya_score, game, term


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=20)
    ap.add_argument("--skill", type=int, default=5)
    ap.add_argument("--elo", type=int, default=0)
    ap.add_argument("--movetime", type=float, default=0.1)
    ap.add_argument("--hash", type=int, default=256)
    ap.add_argument("--target", type=float, default=0.75)
    args = ap.parse_args()

    if not ADITYA.exists():
        sys.exit(f"missing engine binary: {ADITYA}")
    if not STOCKFISH.exists():
        sys.exit(f"missing stockfish: {STOCKFISH}")

    RESULTS.mkdir(exist_ok=True)
    label = f"elo{args.elo}" if args.elo > 0 else f"skill{args.skill}"

    aditya = chess.engine.SimpleEngine.popen_uci(str(ADITYA))
    sf = chess.engine.SimpleEngine.popen_uci(str(STOCKFISH))
    game_results = []
    scores = []

    try:
        aditya.configure({"Hash": args.hash})
        sf.configure({"Hash": args.hash, "Threads": 1})
        if args.elo > 0:
            sf.configure({"UCI_LimitStrength": True, "UCI_Elo": args.elo})
        else:
            sf.configure({"Skill Level": args.skill})

        stamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
        games_path = RESULTS / f"match_{label}_{stamp}.pgn"

        with games_path.open("w", encoding="utf-8") as pgn_out:
            for i in range(args.games):
                aditya_white = (i % 2 == 0)
                score, game, term = play_game(aditya, sf, aditya_white, args.movetime)
                scores.append(score)
                row = {
                    "game": i + 1,
                    "aditya_white": aditya_white,
                    "score": score,
                    "termination": term,
                    "result": game.headers.get("Result"),
                }
                game_results.append(row)
                print(game, file=pgn_out, end="\n\n")
                pgn_out.flush()
                avg = sum(scores) / len(scores)
                print(
                    f"game {i+1}/{args.games}: aditya={'W' if aditya_white else 'B'} "
                    f"score={score} term={term} running={avg:.3f}",
                    flush=True,
                )

        avg = sum(scores) / len(scores)
        summary = {
            "label": label,
            "movetime": args.movetime,
            "points": sum(scores),
            "games": len(scores),
            "score": avg,
            "target": args.target,
            "passed": avg >= args.target,
            "game_results": game_results,
        }
        out_json = RESULTS / f"summary_{label}.json"
        out_json.write_text(json.dumps(summary, indent=2), encoding="utf-8")
        print(json.dumps(summary, indent=2))
        print(f"PGN: {games_path}")
        return 0 if summary["passed"] else 2
    finally:
        aditya.quit()
        sf.quit()


if __name__ == "__main__":
    sys.exit(main())
