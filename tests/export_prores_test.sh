#!/bin/bash
set -e

APP="./MotionCam\ Player"
IN="sample.mcraw"
OUT="out.mov"

$APP --headless --export "$IN" --out "$OUT"

ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,profile -show_entries format=duration "$OUT"
