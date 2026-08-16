#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Test 3 — Timing dei 3 overlay (keyword → frase → immagine)
#
# Video 1920x1080 @ 30fps, durata 5s (150 frame), con tre overlay in finestre
# temporali NON sovrapposte:
#   00:00.500 → keyword  (testo, zona alta)      frame 15 → 45
#   00:01.500 → frase    (testo, zona centrale)  frame 45 → 90
#   00:03.000 → immagine (blu, zona bassa)       frame 90 → 150
#
# Ogni elemento occupa una REGIONE verticale distinta, quindi la verifica è
# basata sulla posizione. PASS se ogni elemento entra ed esce nel momento
# corretto (fondamentale perché PipelineGen dipenderà dai timestamp).
#
# Nota: per un layer testo senza preset, `position` è l'offset del centro del
# testo dal centro canvas (es. [0, -240] → y≈300). Un offset [0, 0] collassa
# nel percorso "identity transform" e finisce in alto a sinistra, quindi la
# frase usa un offset minimo [0, 5] per restare centrata.
#
# Env:
#   CHRONON3D_RUNTIME_IMAGE  immagine runtime Chronon (default: ghcr.io/marcuss-ops/chronon3d-runtime:0.1.0)
#   CHRONON3D_CLI_BIN        se impostato, usa questo binario host invece di docker
#   CHRONON3D_TEST_OUT       directory di output (default: /tmp/chronon_test3)
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SUITE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNTIME_IMAGE="${CHRONON3D_RUNTIME_IMAGE:-ghcr.io/marcuss-ops/chronon3d-runtime:0.1.0}"
OUT="${CHRONON3D_TEST_OUT:-/tmp/chronon_test3}"

mkdir -p "$OUT"
MP4="$OUT/overlay.mp4"

echo "== Test 3: timing dei 3 overlay (keyword → frase → immagine) =="

# ── Fixture: quadrato blu 200x200 per l'overlay immagine. ──────────────────
python3 - "$OUT/blue.png" <<'PY'
import sys
from PIL import Image
Image.new("RGB", (200, 200), (30, 60, 255)).save(sys.argv[1])
PY

if [[ -n "${CHRONON3D_CLI_BIN:-}" ]]; then
    "$CHRONON3D_CLI_BIN" render-plan \
        --input "$SUITE_DIR/plan.json" \
        --assets-root "$ROOT" \
        --output "$MP4"
else
    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    mkdir -p "$WORK/assets/fonts" "$WORK/output"
    cp "$SUITE_DIR/plan.json" "$WORK/plan.json"
    cp "$ROOT/assets/fonts/Poppins-Bold.ttf" "$WORK/assets/fonts/"
    cp "$OUT/blue.png" "$WORK/assets/blue.png"
    chmod -R a+rX "$WORK"
    chmod 777 "$WORK/output"

    docker run --rm \
        -v "$WORK:/work" \
        "$RUNTIME_IMAGE" \
        render-plan \
        --input /work/plan.json \
        --assets-root /work \
        --output /work/output/overlay.mp4

    cp "$WORK/output/overlay.mp4" "$MP4"
fi

echo "PASS exit code: 0"
if [[ ! -s "$MP4" ]]; then
    echo "FAIL overlay.mp4 mancante o vuoto" >&2
    exit 1
fi
echo "PASS file presente: $(stat -c%s "$MP4") byte (> 0)"

# ── Verifica timing per regione ────────────────────────────────────────────
python3 - "$MP4" <<'PY'
import sys, subprocess, io
import numpy as np
from PIL import Image

mp4 = sys.argv[1]
BG = np.array([13, 13, 20])  # sfondo ~ [0.05, 0.05, 0.08] * 255

def frame_at(t):
    raw = subprocess.check_output(
        ["ffmpeg", "-v", "error", "-ss", str(t), "-i", mp4,
         "-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "-"])
    return np.asarray(Image.open(io.BytesIO(raw)).convert("RGB")).astype(int)

def region_content(im, y0, y1, x0=300, x1=1620, thr=500):
    roi = im[y0:y1, x0:x1]
    return int((np.abs(roi - BG).sum(axis=2) > 60).sum()) > thr

# (t_sec, top_keyword, center_phrase, bottom_image)
cases = [
    (0.25, False, False, False),
    (0.50, True,  False, False),
    (1.25, True,  False, False),
    (1.50, False, True,  False),
    (2.50, False, True,  False),
    (3.00, False, False, True),
    (4.50, False, False, True),
    (4.90, False, False, True),
]

fails = 0
for t, exp_top, exp_center, exp_bottom in cases:
    im = frame_at(t)
    top    = region_content(im, 180, 430)   # keyword
    center = region_content(im, 460, 600)   # frase
    bottom = region_content(im, 640, 1040)  # immagine
    ok = (top == exp_top) and (center == exp_center) and (bottom == exp_bottom)
    got = ("TOP" if top else "-", "CENTER" if center else "-", "BOTTOM" if bottom else "-")
    print(f"  t={t:>4}s -> {got} {'PASS' if ok else 'FAIL'}")
    if not ok:
        fails += 1

if fails:
    print(f"FAIL: {fails} punti di controllo non rispettano il timing atteso")
    sys.exit(1)

print("PASS: ogni overlay entra ed esce nel momento corretto")
PY

echo ""
echo "Test 3: PASS — timing dei 3 overlay verificato ($MP4)"
