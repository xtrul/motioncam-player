#!/usr/bin/env bash
set -e
if [ $# -lt 1 ]; then
    echo "Usage: $0 <file.mcraw>" >&2
    exit 1
fi
SCRIPT_DIR="$(dirname "$0")"
EXE="$SCRIPT_DIR/build/mcraw_thumbnail"
if [ ! -x "$EXE" ]; then
    echo "mcraw_thumbnail not found; build it first (see README)." >&2
    exit 1
fi
TMP_PNG="$(mktemp --suffix=.png)"
"$EXE" "$1" "$TMP_PNG"
# Try to open the preview using the default viewer
if command -v xdg-open >/dev/null 2>&1; then
    xdg-open "$TMP_PNG" >/dev/null 2>&1 &
elif command -v open >/dev/null 2>&1; then
    open "$TMP_PNG" >/dev/null 2>&1 &
elif command -v start >/dev/null 2>&1; then
    start "" "$TMP_PNG" >/dev/null 2>&1
fi
echo "Preview saved to $TMP_PNG"
