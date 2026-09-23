#!/usr/bin/env bash
set -euo pipefail

cli=${1:-"$(cd "$(dirname "$0")/../../../build/chronon/linux-video-release/apps/chronon3d_cli" && pwd)/chronon3d_cli"}
fixture_dir=$(cd "$(dirname "$0")" && pwd)
out_dir=$(mktemp -d "${TMPDIR:-/tmp}/chronon-image-residency.XXXXXX")
trap 'rm -rf "$out_dir"' EXIT

for backend in software vulkan; do
  for plan in static animated; do
    "$cli" render --plan "$fixture_dir/$plan.plan.json" \
      --assets-root "$fixture_dir" --backend "$backend" \
      --start-frame 75 --end-frame 75 \
      --output "$out_dir/$plan-$backend.mp4" --report
    ffmpeg -hide_banner -loglevel error -y -i "$out_dir/$plan-$backend.mp4" \
      -frames:v 1 "$out_dir/$plan-$backend.png"
  done
done

python3 - "$out_dir" <<'PY'
from pathlib import Path
import sys
import numpy as np
from PIL import Image

root = Path(sys.argv[1])
images = {
    name: np.asarray(Image.open(root / f"{name}.png").convert("RGB"), dtype=np.int16)
    for name in ("static-software", "animated-software", "static-vulkan", "animated-vulkan")
}
# The asset's center stripe is yellow. H.264 tolerances allow minor chroma loss.
expected = np.array([250, 235, 40], dtype=np.int16)
for name, image in images.items():
    center = image[540, 960]
    error = int(np.abs(center - expected).max())
    print(f"{name}: frame=75 center_rgb={center.tolist()} max_error={error}")
    if error > 32:
        raise SystemExit(f"{name}: missing image content at the center probe")
for backend in ("software", "vulkan"):
    delta = np.abs(images[f"static-{backend}"] - images[f"animated-{backend}"])
    changed = int(np.any(delta > 8, axis=2).sum())
    print(f"{backend}: static_vs_animated_changed_pixels={changed}")
    if changed < 1000:
        raise SystemExit(f"{backend}: image animation did not change content pixels")
PY
