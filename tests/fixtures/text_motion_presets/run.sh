#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../" && pwd)"
CLI="$ROOT/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli"
PLAN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/plans" && pwd)"
OUT="$ROOT/ChrononTemplate/out/text_motion_presets_v1"
mkdir -p "$OUT"
[[ -x "$CLI" ]] || { echo "Missing built CLI: $CLI" >&2; exit 2; }
for plan in "$PLAN_DIR"/*.plan.json; do
  name="$(basename "$plan" .plan.json)"
  "$CLI" render --plan "$plan" --assets-root "$ROOT/Chronon3d" --dry-run --backend software --profile draft >"$OUT/$name.validate.json"
  "$CLI" render --plan "$plan" --assets-root "$ROOT/Chronon3d" --output "$OUT/$name.mp4" --backend software --profile draft --report >"$OUT/$name.log" 2>&1
  frames="$(ffprobe -v error -select_streams v:0 -show_entries stream=nb_frames -of csv=p=0 "$OUT/$name.mp4")"
  [[ "$frames" == 90 ]] || { echo "FAIL $name: expected 90 frames, got $frames" >&2; exit 1; }
  echo "PASS $name: $frames frames"
done
