# adityahallucinates

A from-scratch C++ chess engine aiming for strong classical play (alpha-beta + handcrafted eval), measured against Stockfish skill levels.

## Current strength (local matches)

| Opponent | Score | Notes |
|----------|-------|-------|
| Stockfish Skill 0 | ≥75% | pass |
| Stockfish Skill 1 | ~90% | pass |
| Stockfish Skill 2 | ~90% | pass |
| Stockfish Skill 3 | ~78% | pass |
| Stockfish Skill 4 | ~50–72% | iterating |

Target: **≥75%** score at each skill step while climbing.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
./build/perft_tests
```

Binary: `build/aditya` (UCI).

## Stockfish matches

```bash
./scripts/fetch_stockfish.sh
pip install python-chess
python3 scripts/match_stockfish.py --skill 3 --games 16 --movetime 0.2 --target 0.75
```

Use `--elo N` for `UCI_LimitStrength` instead of Skill Level.

## Architecture

Bitboards + magic sliding attacks, legal movegen (perft-checked), PeSTO-style eval, PVS search with TT/NMP/LMR/quiescence, UCI.

See [PLAN.md](PLAN.md) for the longer NNUE/SPRT roadmap.
