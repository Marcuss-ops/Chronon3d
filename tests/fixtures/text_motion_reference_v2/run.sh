#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../" && pwd)"
CLI="$ROOT/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli"
PLAN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/plans" && pwd)"
OUT="$ROOT/ChrononTemplate/out/text_motion_reference_v3_1080p"
mkdir -p "$OUT"
[[ -x "$CLI" ]] || { echo "Missing built CLI: $CLI" >&2; exit 2; }
GENERATOR="$(mktemp /tmp/motion-reference-generator.XXXXXX)"
trap 'rm -f "$GENERATOR"' EXIT
g++ -std=c++20 -O2 "$(dirname "${BASH_SOURCE[0]}")/generate.cpp" -o "$GENERATOR"
"$GENERATOR" "$(dirname "${BASH_SOURCE[0]}")"
convert "$(dirname "${BASH_SOURCE[0]}")/motion_bear_stage.ppm" "$(dirname "${BASH_SOURCE[0]}")/motion_bear_stage.png"
for plan in "$PLAN_DIR"/*.plan.json; do
  name="$(basename "$plan" .plan.json)"
  frames="$(jq -r '.canvas.duration_frames' "$plan")"
  "$CLI" render --plan "$plan" --assets-root "$ROOT/Chronon3d" --dry-run --backend software --profile draft >"$OUT/$name.validate.json"
  "$CLI" render --plan "$plan" --assets-root "$ROOT/Chronon3d" --output "$OUT/$name.mp4" --backend software --encoder-backend pipe --profile draft --report >"$OUT/$name.log" 2>&1
  actual="$(ffprobe -v error -select_streams v:0 -show_entries stream=nb_frames,r_frame_rate -of csv=p=0 "$OUT/$name.mp4")"
  [[ "$actual" == "30/1,$frames" ]] || { echo "FAIL $name: expected $frames frames @ 30 fps, got $actual" >&2; exit 1; }
  echo "PASS $name: $actual"
done
cp "$(dirname "${BASH_SOURCE[0]}")/manifest.json" "$OUT/manifest.json"
