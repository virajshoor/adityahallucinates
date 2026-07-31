# Next session — continue from here

## Current checkpoint (stop here)

Classical UCI engine `build/aditya` is runnable and strength-tested vs Stockfish 17.1.

| Gate | TC | Result |
|------|-----|--------|
| Skill 4 | 0.25s/move | **78.1% pass** |
| `UCI_Elo` 2000 | 0.25s/move | **75% pass** |
| `UCI_Elo` 2100 | **1.5s/move** | **78.1% pass** |
| Skill 5 | 0.5s/move | ~33% fail |
| `UCI_Elo` 2200 | 1.5s/move | ~41% fail |

**Default eval is classical.** NNUE exists (`nets/fast.nnue`, `ADITYA_USE_NNUE=1`) but is still slower/weaker than classical — do not enable for matches until it wins a classical SPRT.

**Elo 5000 is not a real ladder target.** Stockfish `UCI_Elo` only goes **1320–3190**; full SF ≈3600.

Branch: `cursor/chess-engine-strength-5bbd`  
PR: https://github.com/virajshoor/adityahallucinates/pull/1

---

## Make it runnable (every machine)

```bash
# Toolchain: use g++ (clang often fails linking libstdc++ here)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
./build/perft_tests
./build/aditya   # UCI stdin

# Opponent for matches
./scripts/fetch_stockfish.sh
pip install python-chess

# Sanity match (should still clear)
python3 scripts/match_stockfish.py --elo 2000 --games 16 --movetime 0.25 --target 0.75
python3 scripts/match_stockfish.py --elo 2100 --games 16 --movetime 1.5 --target 0.75
```

Play in a GUI: point Arena / Cute Chess / Nibbler at `build/aditya`.

---

## What to do next (priority order)

### 1. Confirm checkpoint (short)
- Re-run Elo 2000 @ 0.25s and Elo 2100 @ 1.5s (16 games each).
- If either drops under 75%, treat as regression before climbing.

### 2. Climb Elo 2200 (main goal)
- Start at **2–3s/move**, 16–20 games:
  ```bash
  python3 scripts/match_stockfish.py --elo 2200 --games 16 --movetime 3.0 --target 0.75
  ```
- If still ~40–55%, do **not** only raise time — improve the engine (below).
- Then Skill 5 at the same TC as Elo 2100 (1.5s+).

### 3. Classical strength work (highest ROI until NNUE is fast)
- Search: careful ProbCut / singular extensions (last attempt of aggressive singular/ProbCut **regressed** Elo 2000 — keep changes SPRT’d).
- Eval: threats, pawn structure, king safety tuning; avoid expensive full attack-map eval each node.
- Book: keep expanding mainlines that lose vs limited SF.
- Optional: 2-thread Lazy SMP sharing TT (Threads UCI option currently max 1).

### 4. Make NNUE actually usable
Only enable in matches after it beats classical head-to-head.

1. More SF labels (quiet positions, depth ≥8):
   ```bash
   python3 tools/datagen/gen_sf_labels.py --games 200 --depth 8 --out tools/datagen/output/sf_new.bin --use-aditya
   cat tools/datagen/output/all3.bin tools/datagen/output/sf_new.bin > tools/datagen/output/all4.bin
   ```
2. Train fast net:
   ```bash
   python3 tools/train/train_nnue_fast.py --data tools/datagen/output/all4.bin --out nets/fast.nnue --epochs 16
   ```
3. **Incremental int16 accumulator** (refresh-only is ~5× slower than classical — this is the blocker).
4. SPRT classical vs `ADITYA_USE_NNUE=1 ADITYA_NNUE_BLEND=0` (or blend 30–50) at fixed TC.
5. Promote NNUE only if it wins.

### 5. Higher ladder toward max measurable SF
After 2200: 2400 → 2600 → 2800 → 3000 → 3190, then unrestricted Stockfish (no `UCI_LimitStrength`). Expect NNUE + lots of Fishtest-style testing for the upper end.

---

## Pitfalls already learned

- **Do not** claim Elo 5000 via `UCI_Elo` — max is 3190.
- Aggressive singular + ProbCut **tanked** Elo 2000 (~56%); safer search restored 75%.
- Pure/float NNUE crushed strength and NPS; keep classical default.
- Build with **g++**, not clang, in this environment.
- Long matches: 16 games × 1.5–3s/move × ~80–150 plies ≈ **hours** of wall time — that is expected, not a hang.
- Match harness writes PGN at end of each game; empty PGN mid-run can be buffering.

---

## Useful paths

| Path | Role |
|------|------|
| `src/search/search.cpp` | PVS / pruning |
| `src/eval/eval.cpp` | Classical eval (+ optional NNUE blend) |
| `src/nnue/nnue.cpp` | NNUE inference |
| `src/search/book.cpp` | Opening book |
| `scripts/match_stockfish.py` | Strength ladder |
| `tools/datagen/gen_sf_labels.py` | SF-labeled data |
| `tools/train/train_nnue_fast.py` | AHNNUEF2 trainer → `nets/fast.nnue` |
| `results/summary_*.json` | Latest score per opponent |
