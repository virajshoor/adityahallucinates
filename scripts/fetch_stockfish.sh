#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party"
mkdir -p "$DEST"
URL="https://github.com/official-stockfish/Stockfish/releases/download/sf_17.1/stockfish-ubuntu-x86-64-avx2.tar"
TAR="$DEST/stockfish.tar"
if [[ ! -x "$DEST/stockfish/stockfish-ubuntu-x86-64-avx2" ]]; then
  curl -L -o "$TAR" "$URL"
  tar -xf "$TAR" -C "$DEST"
  chmod +x "$DEST/stockfish/stockfish-ubuntu-x86-64-avx2"
  rm -f "$TAR"
fi
echo "Stockfish ready: $DEST/stockfish/stockfish-ubuntu-x86-64-avx2"
