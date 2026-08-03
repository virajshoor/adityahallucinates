# adityahallucinates

A from-scratch C++ chess engine (classical alpha-beta + handcrafted eval), strength-tested against Stockfish 17.1.

## Strength ladder (local matches)

| Opponent | Time/move | Score | Status |
|----------|----------:|------:|--------|
| SF Skill 0–3 | ~0.25s | ≥75–90% | pass |
| SF Skill **4** | 0.25s | **78.1%** | **pass** |
| SF Skill **5** | **1.5s** | **90.6%** | **pass** |
| SF `UCI_Elo` 1400–1800 | ~0.25s | ≥80% | pass |
| SF `UCI_Elo` **2000** | 0.25s | **96.9%** | **pass** |
| SF `UCI_Elo` **2100** | **1.5s** | **100%** | **pass** |
| SF `UCI_Elo` **2200** | **3.0s** | **100%** | **pass** |
| SF `UCI_Elo` **2400** | **3.0s** | **90.6%** | **pass** |
| SF `UCI_Elo` **2600** | **5.0s** | **75%** | **pass** |
| SF `UCI_Elo` **2800** | **5.0s** | **75%** | **pass** |
| SF `UCI_Elo` 3000 | 5.0s | **v16 50%; v17–v19 failed (43.8/34.4/21.9); v21 pure-v16 rematch (baseline 56.3%)** | ceiling (Black weak) |

**Primary 75% gate:** cleared through Stockfish `UCI_Elo` **2800** @5s (and Skill 5).

**Key fixes:** PeSTO PST rank orientation; SEE threat eval + mover color/pins; tempo polarity; full Hash TT.

**Note on “Elo 5000”:** Stockfish `UCI_Elo` only calibrates **1320–3190**; full unrestricted SF is ~3600. There is no Elo-5000 opponent on this ladder — the goal is to climb Skill / `UCI_Elo` / full SF as far as possible.

**Continue later:** see [next.md](next.md) for the exact checkpoint, how to run, and the next strength steps.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
./scripts/smoke_run.sh       # perft + one UCI move
./build/aditya               # UCI (stdin)
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
- Optional NNUE with incremental accumulators (`ADITYA_USE_NNUE=1`, classical default — stronger/faster for now)
- Expanded weighted opening book
- UCI protocol

See [PLAN.md](PLAN.md) for NNUE / SPRT roadmap.
