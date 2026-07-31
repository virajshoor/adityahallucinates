# Next session — continue from here

## Current checkpoint (stop here)

Classical UCI engine `build/aditya` is runnable and strength-tested vs Stockfish 17.1.

| Gate | TC | Result |
|------|-----|--------|
| Skill 4 | 0.25s/move | **78.1% pass** (prior) |
| `UCI_Elo` 2000 | 0.25s/move | **96.9% pass** (retest after PST fix) |
| `UCI_Elo` 2100 | **1.5s/move** | **100% pass (16/16)** |
| `UCI_Elo` 2200 | 3.0s/move | **100% pass (16/16)** |
| `UCI_Elo` 2400 | 3.0s/move | **in progress** |

**Default eval is classical.** Critical fix this session: PeSTO PSTs were rank-flipped (a1=0 vs rank-8-first tables), inflating scores by ~300–500cp and causing exchange blunders. SEE-based threat eval now punishes winning opponent captures (e.g. BxR on a “defended” rook).

NNUE (`nets/fast.nnue`, `ADITYA_USE_NNUE=1`) has incremental int16 dual-perspective accumulators wired through search, but the bootstrap net (~10k SF depth-7 labels) is still weaker/slower than classical — do not enable for matches until it wins a classical SPRT.

**Elo 5000 is not a real ladder target.** Stockfish `UCI_Elo` only goes **1320–3190**; full SF ≈3600.

Branch: `cursor/chess-engine-elo-climb-936e`  
PR: https://github.com/virajshoor/adityahallucinates/pull/2

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
python3 scripts/match_stockfish.py --elo 2200 --games 16 --movetime 3.0 --target 0.75
```

Play in a GUI: point Arena / Cute Chess / Nibbler at `build/aditya`.

---

## What to do next (priority order)

### 1. Confirm Elo 2400+
- Elo 2200 @3.0s cleared **16/16**. Finish Elo 2400 @3.0s (running).
- Optionally retest Elo 2200 @1.5–2.0s; Skill 5 @≥1.5s.

### 2. Climb higher ladder toward max measurable SF
After 2400: 2600 → 2800 → 3000 → 3190, then unrestricted Stockfish (no `UCI_LimitStrength`).

### 3. Classical strength work (highest ROI until NNUE is fast)
- Search: ProbCut is conservative (depth 5–9); keep SPRT’d. Singular extensions remain cautious after prior Elo 2000 regression.
- Eval: threats via SEE are in; further pawn/king tuning OK if SPRT’d.
- Book: expanded (Caro Modern, Open Sicilian, Dutch, London, Ruy) — keep mining losses.
- Optional: 2-thread Lazy SMP sharing TT (Threads UCI option currently max 1).

### 4. Make NNUE actually usable
Only enable in matches after it beats classical head-to-head.

1. More SF labels (quiet positions, depth ≥8):
   ```bash
   python3 tools/datagen/gen_sf_labels.py --games 200 --depth 8 --out tools/datagen/output/sf_new.bin --use-aditya
   cat tools/datagen/output/sf_d7.bin tools/datagen/output/sf_new.bin > tools/datagen/output/all4.bin
   ```
2. Train fast net:
   ```bash
   python3 tools/train/train_nnue_fast.py --data tools/datagen/output/all4.bin --out nets/fast.nnue --epochs 16
   ```
3. Incremental int16 accumulator is **done** (search stack dual-perspective updates). Remaining: int16 MLP forward (float hidden still costs NPS).
4. SPRT classical vs `ADITYA_USE_NNUE=1 ADITYA_NNUE_BLEND=0` (or blend 30–50) at fixed TC.
5. Promote NNUE only if it wins.

---

## Pitfalls already learned

- **Do not** claim Elo 5000 via `UCI_Elo` — max is 3190.
- Aggressive singular + ProbCut **tanked** Elo 2000 (~56%); safer search restored 75%.
- **Flipped PeSTO PSTs** caused massive eval inflation and hanging-exchange blunders — always verify table orientation vs `a1=0`.
- Pure/float NNUE crushed strength and NPS; keep classical default until net + int16 MLP win.
- Build with **g++**, not clang, in this environment.
- Long matches: 16 games × 1.5–3s/move × ~80–150 plies ≈ **hours** of wall time — that is expected, not a hang.
- Match harness writes PGN at end of each game; empty PGN mid-run can be buffering.

---

## Useful paths

| Path | Role |
|------|------|
| `src/search/search.cpp` | PVS / pruning / ProbCut / incremental NNUE hooks |
| `src/eval/eval.cpp` | Classical eval (+ optional NNUE blend) |
| `src/nnue/nnue.cpp` | NNUE inference + dual accumulator |
| `src/search/book.cpp` | Opening book |
| `scripts/match_stockfish.py` | Strength ladder |
| `tools/datagen/gen_sf_labels.py` | SF-labeled data |
| `tools/train/train_nnue_fast.py` | AHNNUEF2 trainer → `nets/fast.nnue` |
| `results/summary_*.json` | Latest score per opponent |
