#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
source ~/.xmake/profile 2>/dev/null || true
mkdir -p output

echo "[Mask Proofs] Building..."
xmake f -m debug --profiling=false
xmake -y

run() { local name="$1" frame="$2" out="$3"
    echo "[Mask Proofs] $name frame $frame -> $out"
    xmake run -w . chronon3d_cli render "$name" --frame "$frame" -o "$out"
    [[ -s "$out" ]] || { echo "ERROR: $out empty"; exit 1; }
}

run MaskRectProof            0   output/mask_rect.png
run MaskRoundedRectProof     0   output/mask_rounded_rect.png
run MaskCircleProof          0   output/mask_circle.png
run MaskTextRevealProof      0   output/mask_text_reveal.png
run AnimatedMaskRevealProof  0   output/mask_reveal_0000.png
run AnimatedMaskRevealProof  30  output/mask_reveal_0030.png
run AnimatedMaskRevealProof  60  output/mask_reveal_0060.png
run MaskLayerTransformProof  0   output/mask_layer_transform.png
run MaskCamera25DProof       0   output/mask_camera25d_0000.png
run MaskCamera25DProof       30  output/mask_camera25d_0030.png
run MaskCamera25DProof       60  output/mask_camera25d_0060.png

echo "[Mask Proofs] Done."
ls -lh output/mask_*.png
