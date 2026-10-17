#!/usr/bin/env bash
# Render the v2.0 documentary demos on the Vulkan (GPU) backend.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
CLI="$ROOT/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli"
OUT="$HERE/out"
mkdir -p "$OUT"
: > "$OUT/gpu_status.txt"
for n in entity_line_h4_v2 family_tree_1p4_v2; do
  "$CLI" render --plan "$HERE/plans/$n.plan.json" -o "$OUT/$n.mp4" \
    --assets-root "$ROOT" --backend vulkan > "$OUT/$n.render.log" 2>&1
  echo "$n exit=$?" >> "$OUT/gpu_status.txt"
done
