#!/usr/bin/env bash
set -e
if [ $# -lt 1 ]; then
    echo "Usage: $0 <folder-with-mcraw-files>" >&2
    exit 1
fi
INPUT_DIR="$1"
SCRIPT_DIR="$(dirname "$0")"
EXE="$SCRIPT_DIR/build/mcraw_thumbnail"
if [ ! -x "$EXE" ]; then
    echo "mcraw_thumbnail not found; build it first (see README)." >&2
    exit 1
fi
shopt -s nullglob
for f in "$INPUT_DIR"/*.mcraw; do
    out="${f%.mcraw}.png"
    "$EXE" "$f" "$out"
    echo "Generated $out"
done
