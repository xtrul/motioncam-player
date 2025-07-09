#!/bin/bash
set -e
APP="./MotionCam Player"
IN="sample.mcraw"
OUT="out.mov"
$APP --export-prores gpu "$IN" "$OUT" || true
ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,profile -of default=nw=1 "$OUT"
