#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

source ~/.xmake/profile 2>/dev/null || true

mkdir -p output

echo "[Camera2.5D Proofs] Building..."
xmake f -m debug --profiling=false
xmake -y

run() {
    local name="$1" frame="$2" out="$3"
    echo "[Camera2.5D Proofs] Rendering $name frame $frame -> $out"
    xmake run -w . chronon3d_cli render "$name" --frame "$frame" -o "$out"
    [[ -s "$out" ]] || { echo "ERROR: $out not created or empty"; exit 1; }
}

run Camera25DDepthScaleProof    0   output/camera25d_depth_scale.png

run Camera25DParallaxProof      0   output/camera25d_parallax_0000.png
run Camera25DParallaxProof      30  output/camera25d_parallax_0030.png
run Camera25DParallaxProof      60  output/camera25d_parallax_0060.png

run Camera25DZOrderProof        0   output/camera25d_z_order.png

run Camera25DCameraPushProof    0   output/camera25d_push_0000.png
run Camera25DCameraPushProof    30  output/camera25d_push_0030.png
run Camera25DCameraPushProof    60  output/camera25d_push_0060.png

run Camera25DMixed2D3DProof     0   output/camera25d_mixed_0000.png
run Camera25DMixed2D3DProof     59  output/camera25d_mixed_0059.png

run Camera25DImageParallaxProof 0   output/camera25d_image_parallax_0000.png
run Camera25DImageParallaxProof 30  output/camera25d_image_parallax_0030.png
run Camera25DImageParallaxProof 60  output/camera25d_image_parallax_0060.png

echo ""
echo "[Camera2.5D Proofs] Done."
ls -lh output/camera25d_*.png
