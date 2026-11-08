#!/usr/bin/env bash
# Reproduce the broadcast six-test evidence bundle.
#
# Requires: a Release CLI built with CHRONON3D_ENABLE_TEXT=ON, ffmpeg, python3
# with numpy/opencv/Pillow.  Raw frames live in TMPDIR (about 2.3 GiB) and are
# not part of the repository.  The default render backend is Vulkan; set
# CHRONON_BACKEND=software only for a CPU comparison run.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
CLI="$ROOT/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli"
TMP="${TMPDIR:-/tmp}/chronon-broadcast-six"
BACKEND="${CHRONON_BACKEND:-vulkan}"
LOGS="$HERE/logs"
mkdir -p "$TMP" "$LOGS"

if [[ ! -x "$CLI" ]]; then
    echo "missing CLI: $CLI" >&2
    exit 2
fi

render_raw() {
    local plan="$1" start="$2" end="$3" output="$4" log="$5"
    echo "render $(basename "$plan") frames $start..$end backend=$BACKEND"
    "$CLI" render --plan "$plan" --assets-root "$ROOT" --backend "$BACKEND" \
        --start-frame "$start" --end-frame "$end" --video-sink raw \
        --output "$output" >"$log" 2>&1
    if [[ ! -s "$output" ]]; then
        echo "missing raw output: $output" >&2
        tail -30 "$log" >&2
        exit 1
    fi
}

encode() {
    local raw="$1" width="$2" height="$3" fps="$4" output="$5"
    local pad=()
    if (( height % 2 != 0 )); then
        # H.264 4:2:0 requires even dimensions.  The 405-row mobile preview
        # gets one black row; all metrics still read the 720x405 raw.
        pad=(-vf "pad=${width}:$((height + 1)):0:0:black")
    fi
    echo "encode $(basename "$output")"
    ffmpeg -y -v error -f rawvideo -pix_fmt rgba -s "${width}x${height}" \
        -r "$fps" -i "$raw" "${pad[@]}" -c:v libx264 -preset medium -crf 18 \
        -pix_fmt yuv420p -movflags +faststart "$output"
}

render_raw "$HERE/broadcast_canary.plan.json" 60 90 \
    "$TMP/broadcast_canary_1080p.raw" "$LOGS/render_canary.log"
encode "$TMP/broadcast_canary_1080p.raw" 1920 1080 30 \
    "$HERE/broadcast_canary_1080p.mp4"

render_raw "$HERE/broadcast_hierarchy_60fps.plan.json" 0 119 \
    "$TMP/broadcast_hierarchy_60fps.raw" "$LOGS/render_hierarchy.log"
encode "$TMP/broadcast_hierarchy_60fps.raw" 960 540 60 \
    "$HERE/broadcast_hierarchy_60fps.mp4"

render_raw "$HERE/subpixel_4k.plan.json" 0 29 \
    "$TMP/subpixel_4k.raw" "$LOGS/render_subpixel_4k.log"
encode "$TMP/subpixel_4k.raw" 3840 2160 30 "$HERE/subpixel_4k.mp4"

render_raw "$HERE/subpixel_720.plan.json" 0 29 \
    "$TMP/subpixel_720.raw" "$LOGS/render_subpixel_720.log"
encode "$TMP/subpixel_720.raw" 720 405 30 "$HERE/subpixel_720.mp4"

python3 "$ROOT/tools/broadcast_six_diagnostics.py" \
    --plan "$HERE/broadcast_canary.plan.json" \
    --hierarchy-plan "$HERE/broadcast_hierarchy_60fps.plan.json" \
    --morph "$HERE/broadcast_canary_morph.svg" \
    --canary-raw "$TMP/broadcast_canary_1080p.raw" \
    --canary-width 1920 --canary-height 1080 \
    --hierarchy-raw "$TMP/broadcast_hierarchy_60fps.raw" \
    --hierarchy-width 960 --hierarchy-height 540 \
    --subpixel-4k-raw "$TMP/subpixel_4k.raw" \
    --subpixel-4k-width 3840 --subpixel-4k-height 2160 \
    --subpixel-720-raw "$TMP/subpixel_720.raw" \
    --subpixel-720-width 720 --subpixel-720-height 405 \
    --audit-dir "$ROOT/docs/evidence/camera-ring-text-drive-20261108" \
    --output "$HERE/report.json"

python3 - "$TMP" "$HERE" <<'PY'
import sys
from pathlib import Path

import numpy as np
from PIL import Image

tmp, out = Path(sys.argv[1]), Path(sys.argv[2])


def frame(path, width, height, index):
    raw = np.fromfile(path, dtype=np.uint8)
    size = width * height * 4
    return raw[index * size:(index + 1) * size].reshape(height, width, 4)


canary = frame(tmp / "broadcast_canary_1080p.raw", 1920, 1080, 30)
Image.fromarray(canary[..., :3], "RGB").save(out / "canary_frame_090.png")

hierarchy = frame(tmp / "broadcast_hierarchy_60fps.raw", 960, 540, 40)
Image.fromarray(hierarchy[..., :3], "RGB").save(out / "hierarchy_frame_040.png")

full4k = frame(tmp / "subpixel_4k.raw", 3840, 2160, 15)
Image.fromarray(full4k[900:1260, 900:1100, :3], "RGB").save(
    out / "subpixel_4k_line_frame_015.png")
Image.fromarray(full4k[700:1100, 2800:3040, :3], "RGB").save(
    out / "subpixel_4k_seam_frame_015.png")

full720 = frame(tmp / "subpixel_720.raw", 720, 405, 15)
Image.fromarray(full720[150:260, 150:240, :3], "RGB").save(
    out / "subpixel_720_line_frame_015.png")
Image.fromarray(full720[130:220, 520:580, :3], "RGB").save(
    out / "subpixel_720_seam_frame_015.png")
print("wrote PNG evidence")
PY

echo "evidence complete: $HERE/report.json"
