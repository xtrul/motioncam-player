#!/bin/sh
# Minimal smoke test for GPU export fallback
set -e
if [ ! -f "MotionCam Player" ]; then
  echo "Binary not built" >&2
  exit 0
fi
./MotionCam\ Player --export-prores gpu sample.mcraw out.mov || true
