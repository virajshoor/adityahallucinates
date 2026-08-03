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
| `UCI_Elo` 2800 | **5.0s/move** | **75% pass (12/16)** |
| `UCI_Elo` 3000 | **5.0s** | **v16 50%** (W62.5/B37.5); **v17 43.8%** (W37.5/B50) — KS boost helped Black, hurt White |

**Default eval is classical.** Critical fixes this session:
1. PeSTO PSTs were rank-flipped (a1=0 vs rank-8-first) — ~300–500cp inflation + exchange blunders
2. SEE-based threat eval for winning opponent captures (e.g. BxR on “defended” rook)
3. SEE mover color + pins + promotion next-victim; pawn `gives_check`
4. Tempo polarity (Black STM was ~80cp too low)
5. Full Hash TT (multiply-high); null-move `MOVE_NULL`; book ply 14
6. Advantage-dependent endgame mop-up; per-victim threat aggregation
7. Search patches (stand-pat/draw/qsearch rewrite) **regressed Elo 2400+** — keep 9848fe5 search skeleton
8. Elo 3000 bottleneck is **Black** (37.5%); distance-based king shelter + QGD book
9. Book gaps closed for QGD+Nf3/Nc3, Catalan, Vienna/3N, Exchange Slav (stop early `...h6` / `...Nge7` / `...Nh5`)
10. Removed early rook-pawn tempo tax (hurt more than it helped)
11. Scotch Gambit book: prefer ...Be7/a6 over ...Bd7 after 6.Bb5 Ne4 7.O-O
12. v7@40%/5 and v8@12.5%/4 aborted; Black defense still the bottleneck

NNUE (`nets/fast.nnue`, `ADITYA_USE_NNUE=1`) has incremental int16 dual-perspective accumulators in search. Bootstrap net (~20k SF labels) loses heavily to classical in short self-play — **do not enable for matches** until it wins SPRT.

**Elo 5000 is not a real ladder target.** Stockfish `UCI_Elo` only goes **1320–3190**; full SF ≈3600. Measurable progress: cleared through **2800**; Elo 3000 at **56.3%**.

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

python3 scripts/match_stockfish.py --elo 2800 --games 16 --movetime 5.0 --target 0.75
python3 scripts/match_stockfish.py --elo 3000 --games 16 --movetime 5.0 --target 0.75
python3 scripts/match_stockfish.py --elo 2600 --games 16 --movetime 5.0 --target 0.75
python3 scripts/match_stockfish.py --elo 2400 --games 16 --movetime 3.0 --target 0.75
```

---

## What to do next (priority order)

### 1. Break Elo 3000 (main goal)
- Cleared Elo 2800 @5s (**75%**)
- Elo 3000 v16 @5s: **50%** (W 62.5% / B 37.5%) — completed full 16 after hang fixes
- Prior baseline 56.3% (W 75% / B 37.5%); Black still the bottleneck
- Hangs fixed: hardDeadline, no qsearch quiet-checks, SEE cap (do not thread-wrap SimpleEngine)
- Solid Black book kept; need **classical eval/search strength**, not more book churn
- v17: stronger king-safety + castled defenders; Elo2400 hold then Elo3000
- Validate search changes at Elo 2400 first; then 3190 → unrestricted SF

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

- Timed search can hang for hours on aspiration/fail-high + deep qsearch checks — hard-cap movetime, depth 48, aspiration tries, and break on stop at every depth.


- **Do not** claim Elo 5000 via `UCI_Elo` — max is 3190
- Flipped PeSTO PSTs caused massive eval inflation — verify table orientation vs `a1=0`
- Aggressive singular/ProbCut regressed Elo 2000; ProbCut currently disabled
- Extra endgame king/passer inflation did **not** help Elo 2800 @8s
- Pure NNUE still loses to classical; keep classical default
- Root-relative draw contempt overpressed at Elo 3000 @8s — reverted to soft-draw
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
