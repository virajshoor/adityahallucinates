# adityahallucinates

A from-scratch C++ chess engine (classical alpha-beta + handcrafted eval), strength-tested against Stockfish 17.1.

## Strength ladder (local, ~0.2s/move)

| Opponent | Score | Status |
|----------|------:|--------|
| SF Skill 0–3 | ≥75% | pass |
| SF Skill 4 | ~50–72% | iterating |
| SF `UCI_Elo` 1400 | 95% | pass |
| SF `UCI_Elo` 1600 | 80% | pass |
| SF `UCI_Elo` **1700** | **75%** | **pass (target)** |
| SF `UCI_Elo` 1800 | 65% | next target |
| SF `UCI_Elo` 2000 | 40% | — |

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
./build/perft_tests          # move-gen correctness
./build/aditya               # UCI engine
```

## Match Stockfish

```bash
./scripts/fetch_stockfish.sh
pip install python-chess
python3 scripts/match_stockfish.py --elo 1700 --games 16 --movetime 0.2 --target 0.75
python3 scripts/match_stockfish.py --skill 3 --games 16 --movetime 0.2 --target 0.75
```

## Engine features

- Bitboards + magic sliding attacks
- Legal move generation (perft suite green)
- PeSTO-style eval (material, PST, mobility, pawns, king safety)
- PVS search: TT, NMP, LMR, futility, quiescence, killers/history
- Small weighted opening book
- UCI protocol

See [PLAN.md](PLAN.md) for NNUE / SPRT roadmap.
