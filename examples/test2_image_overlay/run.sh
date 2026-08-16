#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Test 2 — Overlay immagine (PNG → MP4 reale)
#
# Dimostra che Chronon carica una PNG come overlay con proporzioni corrette:
# una PNG 300x150 (aspect 2:1, verde pieno) dentro una box 600x600 con
# fit=contain deve essere renderizzata 600x300 (aspect 2.0), NON stirata a
# 600x600 (aspect 1.0). L'overlay è visibile solo nell'intervallo 0.5s → 2.5s
# (frame 15 → 75) su canvas 1920x1080 @ 30fps, durata 3s.
#
# Criteri PASS:
#   - exit code 0 (nessun crash)
#   - overlay.mp4 esiste e ha dimensione > 0 byte
#   - l'immagine compare solo nell'intervallo richiesto
#   - proporzioni corrette (aspect della regione renderizzata ≈ 2.0, non 1.0)
#
# Env:
#   CHRONON3D_RUNTIME_IMAGE  immagine runtime Chronon (default: ghcr.io/marcuss-ops/chronon3d-runtime:0.1.0)
#   CHRONON3D_CLI_BIN        se impostato, usa questo binario host invece di docker
#   CHRONON3D_TEST_OUT       directory di output (default: /tmp/chronon_test2)
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SUITE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNTIME_IMAGE="${CHRONON3D_RUNTIME_IMAGE:-ghcr.io/marcuss-ops/chronon3d-runtime:0.1.0}"
OUT="${CHRONON3D_TEST_OUT:-/tmp/chronon_test2}"

mkdir -p "$OUT"
MP4="$OUT/overlay.mp4"

echo "== Test 2: overlay immagine PNG (proporzioni + timing) =="

# ── Fixture: PNG 300x150 verde pieno (aspect 2:1), generata deterministicamente.
python3 - "$OUT/overlay.png" <<'PY'
import sys
from PIL import Image
Image.new("RGB", (300, 150), (0, 224, 0)).save(sys.argv[1])
PY

if [[ -n "${CHRONON3D_CLI_BIN:-}" ]]; then
    "$CHRONON3D_CLI_BIN" render-plan \
        --input "$SUITE_DIR/plan.json" \
        --assets-root "$ROOT" \
        --output "$MP4"
else
    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    mkdir -p "$WORK/assets" "$WORK/output"
    cp "$SUITE_DIR/plan.json" "$WORK/plan.json"
    cp "$OUT/overlay.png" "$WORK/assets/overlay.png"
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

# ── PASS 1: exit code 0 (nessun crash) ─────────────────────────────────────
echo "PASS exit code: 0"

# ── PASS 2: file esiste e > 0 byte ─────────────────────────────────────────
if [[ ! -s "$MP4" ]]; then
    echo "FAIL overlay.mp4 mancante o vuoto" >&2
    exit 1
fi
echo "PASS file presente: $(stat -c%s "$MP4") byte (> 0)"

# ── PASS 3: l'immagine compare solo nell'intervallo ────────────────────────
ffmpeg -v error -y -ss 0.2 -i "$MP4" -frames:v 1 "$OUT/f_before.png"
ffmpeg -v error -y -ss 1.5 -i "$MP4" -frames:v 1 "$OUT/f_image.png"
ffmpeg -v error -y -ss 2.8 -i "$MP4" -frames:v 1 "$OUT/f_after.png"

IMG_AE="$(compare -metric AE "$OUT/f_before.png" "$OUT/f_image.png" null: 2>&1 || true)"
AFTER_AE="$(compare -metric AE "$OUT/f_before.png" "$OUT/f_after.png" null: 2>&1 || true)"

if [[ "$IMG_AE" -lt 10000 ]]; then
    echo "FAIL l'immagine non compare (AE sfondo→immagine = $IMG_AE)" >&2
    exit 1
fi
if [[ "$AFTER_AE" -gt 5000 ]]; then
    echo "FAIL l'immagine non esce correttamente (AE sfondo→fine = $AFTER_AE)" >&2
    exit 1
fi
echo "PASS immagine visibile solo nell'intervallo (AE in=$IMG_AE, AE out=$AFTER_AE)"

# ── PASS 4: proporzioni corrette ───────────────────────────────────────────
# Misura l'aspect ratio della regione verde renderizzata. Con fit=contain una
# box 600x600 e un'immagine 2:1 devono produrre una regione ~600x300 (≈2.0);
# uno stretch (fit sbagliato) produrrebbe 600x600 (≈1.0).
python3 - "$OUT/f_image.png" <<'PY'
import sys
import numpy as np
from PIL import Image

im = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(int)
green = (im[:, :, 1] > 120) & (im[:, :, 0] < 120) & (im[:, :, 2] < 120)
count = int(green.sum())
if count == 0:
    print("FAIL nessun pixel verde trovato"); sys.exit(1)

ys, xs = np.where(green)
w = int(xs.max() - xs.min() + 1)
h = int(ys.max() - ys.min() + 1)
aspect = w / h
print(f"regione verde: {w}x{h} px, aspect={aspect:.3f}")

if not (1.85 <= aspect <= 2.15):
    print(f"FAIL proporzioni errate: aspect={aspect:.3f} (atteso ~2.0, uno stretch darebbe ~1.0)")
    sys.exit(1)
print(f"PASS proporzioni corrette: aspect={aspect:.3f} (~2.0, fit=contain rispettato)")
PY

echo ""
echo "Test 2: PASS — overlay immagine prodotto ($MP4)"
