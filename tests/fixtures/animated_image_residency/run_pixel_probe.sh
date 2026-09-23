#!/usr/bin/env bash
set -euo pipefail

cli=${1:-"$(cd "$(dirname "$0")/../../../build/chronon/linux-video-release/apps/chronon3d_cli" && pwd)/chronon3d_cli"}
fixture_dir=$(cd "$(dirname "$0")" && pwd)
out_dir=$(mktemp -d "${TMPDIR:-/tmp}/chronon-image-residency.XXXXXX")
trap 'rm -rf "$out_dir"' EXIT

for backend in software vulkan; do
  for plan in static animated camera-only; do
    for frame in 75 149; do
      "$cli" render --plan "$fixture_dir/$plan.plan.json" \
        --assets-root "$fixture_dir" --backend "$backend" \
        --start-frame "$frame" --end-frame "$frame" \
        --output "$out_dir/$plan-$backend-$frame.mp4" --report
      ffmpeg -hide_banner -loglevel error -y -i "$out_dir/$plan-$backend-$frame.mp4" \
        -frames:v 1 "$out_dir/$plan-$backend-$frame.png"
    done
  done
done

python3 - "$out_dir" <<'PY'
from pathlib import Path
import sys
import numpy as np
from PIL import Image

root = Path(sys.argv[1])
images = {
    (plan, backend, frame): np.asarray(
        Image.open(root / f"{plan}-{backend}-{frame}.png").convert("RGB"),
        dtype=np.int16)
    for plan in ("static", "animated", "camera-only")
    for backend in ("software", "vulkan")
    for frame in (75, 149)
}
# The asset's center stripe is yellow. H.264 tolerances allow minor chroma loss.
expected = np.array([248, 233, 39], dtype=np.int16)
failures = []
for (plan, backend, frame), image in images.items():
    center = image[540, 960]
    error = int(np.abs(center - expected).max())
    print(f"{plan}-{backend}: frame={frame} center_rgb={center.tolist()} max_error={error}")
    if error > 5:
        failures.append(f"{plan}-{backend} frame {frame}: center pixel outside tolerance")
for plan in ("static", "animated", "camera-only"):
    for frame in (75, 149):
        delta = np.abs(images[(plan, "software", frame)] - images[(plan, "vulkan", frame)])
        max_delta = int(delta.max())
        mean_delta = float(delta.mean())
        print(f"{plan}: frame={frame} software_vs_vulkan max_delta={max_delta} mean_delta={mean_delta:.3f}")
        # Record full-frame deltas as diagnostics; the center pixel is the
        # acceptance sample, while H.264 chroma subsampling can amplify local
        # edge deltas elsewhere in the frame.
if failures:
    raise SystemExit("; ".join(failures))
PY
