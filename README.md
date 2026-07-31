# adityahallucinates

A from-scratch C++ chess engine (classical alpha-beta + handcrafted eval), strength-tested against Stockfish 17.1.

## Strength ladder (local matches)

| Opponent | Time/move | Score | Status |
|----------|----------:|------:|--------|
| SF Skill 0–3 | ~0.25s | ≥75–90% | pass |
| SF Skill **4** | 0.25s | **78.1%** | **pass** |
| SF Skill 5 | 0.5s | ~33% | iterating |
| SF `UCI_Elo` 1400–1800 | ~0.25s | ≥80% | pass |
| SF `UCI_Elo` **2000** | 0.25s | **75%** | **pass** |
| SF `UCI_Elo` **2100** | **1.5s** | **78.1%** | **pass** |
| SF `UCI_Elo` 2200+ | — | climbing | in progress |

**Primary 75% gate:** cleared at Stockfish `UCI_Elo` 2000 (0.25s) and **2100 (1.5s)**, and Skill Level 4.

**Note on “Elo 5000”:** Stockfish `UCI_Elo` only calibrates **1320–3190**; full unrestricted SF is ~3600. There is no Elo-5000 opponent on this ladder — the goal is to climb Skill / `UCI_Elo` / full SF as far as possible.

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
python3 scripts/match_stockfish.py --elo 2100 --games 16 --movetime 1.5 --target 0.75
python3 scripts/match_stockfish.py --elo 2000 --games 16 --movetime 0.25 --target 0.75
python3 scripts/match_stockfish.py --skill 4 --games 16 --movetime 0.25 --target 0.75
```

## Engine features

- Bitboards + magic sliding attacks
- Legal move generation (perft suite green)
- PeSTO-style eval (material, PST, mobility, pawns, king safety, outposts, space, passer king proximity)
- PVS search: TT, NMP, LMR, SEE pruning, qsearch checks, killers/history/countermoves
- Optional bootstrap NNUE (`ADITYA_USE_NNUE=1`, classical default — stronger/faster for now)
- Expanded weighted opening book
- UCI protocol

See [PLAN.md](PLAN.md) for NNUE / SPRT roadmap.
