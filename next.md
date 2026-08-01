# Next session — continue from here

## Current checkpoint (stop here)

Classical UCI engine `build/aditya` is runnable and strength-tested vs Stockfish 17.1.

| Gate | TC | Result |
|------|-----|--------|
| Skill 4 | 0.25s/move | **78.1% pass** (prior) |
| Skill 5 | 1.5s/move | **90.6% pass** |
| `UCI_Elo` 2000 | 0.25s/move | **96.9% pass** |
| `UCI_Elo` 2100 | 1.5s/move | **100% pass (16/16)** |
| `UCI_Elo` 2200 | 3.0s/move | **100% pass (16/16)** |
| `UCI_Elo` 2400 | 3.0s/move | **90.6% pass** |
| `UCI_Elo` 2600 | **5.0s/move** | **75% pass** (71.9% near-miss @3s) |
| `UCI_Elo` 2800 | 5.0s / 8.0s | **retesting** after qsearch/SEE/draw/TT/book fixes (prior 65.6% / 59.4%) |

**Default eval is classical.** Critical fixes this session:
1. PeSTO PSTs were rank-flipped (a1=0 vs rank-8-first) — ~300–500cp inflation + exchange blunders
2. SEE-based threat eval for winning opponent captures (e.g. BxR on “defended” rook)
3. **Qsearch stand-pat while in check** (tactical correctness); quiet promotions in qsearch
4. **SEE mover color** (threat eval was wrong for non-STM); pin filtering; promotion gain
5. Draws before TT; removed inverted soft-draw; book ply gate 8→14; full Hash TT capacity

NNUE (`nets/fast.nnue`, `ADITYA_USE_NNUE=1`) has incremental int16 dual-perspective accumulators in search. Bootstrap net (~20k SF labels) loses heavily to classical in short self-play — **do not enable for matches** until it wins SPRT.

**Elo 5000 is not a real ladder target.** Stockfish `UCI_Elo` only goes **1320–3190**; full SF ≈3600. Measurable progress: cleared through **2600**; **2800+** in retest.

Branch: `cursor/chess-engine-elo-climb-936e`  
PR: https://github.com/virajshoor/adityahallucinates/pull/2

---

## Make it runnable (every machine)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
./build/perft_tests
./scripts/fetch_stockfish.sh
pip install python-chess

python3 scripts/match_stockfish.py --elo 2600 --games 16 --movetime 5.0 --target 0.75
python3 scripts/match_stockfish.py --elo 2400 --games 16 --movetime 3.0 --target 0.75
```

---

## What to do next (priority order)

### 1. Break Elo 2800 (main goal)
- Analyze `results/match_elo2800_*.pgn` losses (many draws + black losses)
- Classical: better endgame conversion, less drawish play when ahead, search quality at long TC
- Try 2800 @5–8s again after each SPRT’d patch
- Then 3000 → 3190 → unrestricted SF

### 2. Skill 5 — done
- Cleared Skill 5 @1.5s (**90.6%**)

### 3. NNUE that beats classical
```bash
python3 tools/datagen/gen_sf_labels.py --games 300 --depth 9 --out tools/datagen/output/sf_d9.bin --use-aditya
cat tools/datagen/output/sf_d7.bin tools/datagen/output/sf_d8b.bin tools/datagen/output/sf_d9.bin > tools/datagen/output/all.bin
python3 tools/train/train_nnue_fast.py --data tools/datagen/output/all.bin --out nets/fast.nnue --epochs 24
# Self-match classical vs ADITYA_USE_NNUE=1 ADITYA_NNUE_BLEND=0
# Int16 MLP forward still pending (float hidden limits NPS)
```

### 4. Optional Lazy SMP (Threads > 1)

---

## Pitfalls already learned

- **Do not** claim Elo 5000 via `UCI_Elo` — max is 3190
- Flipped PeSTO PSTs caused massive eval inflation — verify table orientation vs `a1=0`
- Aggressive singular/ProbCut regressed Elo 2000; ProbCut currently disabled
- Extra endgame king/passer inflation did **not** help Elo 2800 @8s
- Pure NNUE still loses to classical; keep classical default
- Build with **g++**; long matches take hours

---

## Useful paths

| Path | Role |
|------|------|
| `src/search/search.cpp` | PVS / pruning / NNUE hooks |
| `src/eval/eval.cpp` | Classical eval (+ NNUE blend) |
| `src/nnue/nnue.cpp` | NNUE + dual accumulator |
| `src/search/book.cpp` | Opening book |
| `scripts/match_stockfish.py` | Strength ladder |
| `nets/fast.nnue` | Bootstrap AHNNUEF2 net |
| `results/summary_*.json` | Latest scores |
