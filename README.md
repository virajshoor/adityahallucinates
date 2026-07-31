# adityahallucinates

A from-scratch C++ chess engine (classical alpha-beta + handcrafted eval), strength-tested against Stockfish 17.1.

## Strength ladder (local matches, ~0.2–0.3s/move)

| Opponent | Score | Status |
|----------|------:|--------|
| SF Skill 0–3 | ≥75–90% | pass |
| SF Skill **4** | **78.1%** | **pass** |
| SF Skill 5 | ~46% | iterating |
| SF `UCI_Elo` 1400 | 95% | pass |
| SF `UCI_Elo` 1600 | 80% | pass |
| SF `UCI_Elo` 1700 | 92.5% (20 games) | pass |
| SF `UCI_Elo` 1800 | 85% (20 games) | pass |
| SF `UCI_Elo` **2000** | **78.1%** | **pass (75% target)** |
| SF `UCI_Elo` 2100 | ~40–58% | next |
| SF `UCI_Elo` 2200 | ~34% | — |

**Primary 75% gate:** cleared at Stockfish `UCI_Elo` 2000 and Skill Level 4.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
./build/perft_tests
./build/aditya               # UCI
```

## Match Stockfish

```bash
./scripts/fetch_stockfish.sh
pip install python-chess
python3 scripts/match_stockfish.py --elo 2000 --games 16 --movetime 0.25 --target 0.75
python3 scripts/match_stockfish.py --skill 4 --games 16 --movetime 0.25 --target 0.75
```

## Engine features

- Bitboards + magic sliding attacks
- Legal move generation (perft suite green)
- PeSTO-style eval (material, PST, mobility, pawns, king safety)
- PVS search: TT, NMP, LMR, futility, quiescence, killers/history
- Small weighted opening book
- UCI protocol

See [PLAN.md](PLAN.md) for NNUE / SPRT roadmap.
