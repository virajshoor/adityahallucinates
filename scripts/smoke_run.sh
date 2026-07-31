#!/usr/bin/env bash
# Quick local smoke: build (if needed) and run a short UCI move.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -x build/aditya ]]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
  cmake --build build -j"$(nproc)"
fi

echo "perft..."
./build/perft_tests | tail -1

echo "uci smoke..."
printf 'uci\nisready\nposition startpos\ngo movetime 200\nquit\n' | ./build/aditya | grep -E 'id name|uciok|readyok|bestmove'
echo "OK — engine runnable. See next.md for the strength ladder."
