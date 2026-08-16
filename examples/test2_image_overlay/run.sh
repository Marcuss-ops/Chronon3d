#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Test 2 — Overlay immagine (PNG e JPG → MP4 reale)
#
# Dimostra che Chronon carica sia una PNG che una JPG come overlay con
# proporzioni corrette: un'immagine 300x150 (aspect 2:1, colore pieno) dentro
# una box 600x600 con fit=contain deve essere renderizzata 600x300 (aspect
# 2.0), NON stirata a 600x600 (aspect 1.0). L'overlay è visibile solo
# nell'intervallo 0.5s → 2.5s (frame 15 → 75) su canvas 1920x1080 @ 30fps,
# durata 3s.
#
# Criteri PASS (per ciascun formato PNG e JPG):
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

echo "== Test 2: overlay immagine PNG e JPG (proporzioni + timing) =="

# ── Fixture: 300x150 (aspect 2:1) a colore pieno, generate deterministicamente.
#    verde per PNG, magenta per JPG. ────────────────────────────────────────
python3 - "$OUT" <<'PY'
import sys
from PIL import Image
out = sys.argv[1]
Image.new("RGB", (300, 150), (0, 224, 0)).save(f"{out}/overlay.png")
Image.new("RGB", (300, 150), (224, 0, 224)).save(f"{out}/overlay.jpg", quality=95)
PY

# run_case <ext>  — renderizza overlay_<ext>.mp4 e verifica i criteri PASS.
run_case() {
    local ext="$1"
    local mp4="$OUT/overlay_${ext}.mp4"
    local plan="$OUT/plan_${ext}.json"
    sed "s#assets/overlay\.png#assets/overlay.${ext}#" "$SUITE_DIR/plan.json" > "$plan"

    echo "-- ${ext^^}: render --"
    if [[ -n "${CHRONON3D_CLI_BIN:-}" ]]; then
        "$CHRONON3D_CLI_BIN" render-plan \
            --input "$plan" \
            --assets-root "$ROOT" \
            --output "$mp4"
    else
        WORK="$(mktemp -d)"
        trap 'rm -rf "$WORK"' EXIT
        mkdir -p "$WORK/assets" "$WORK/output"
        cp "$plan" "$WORK/plan.json"
        cp "$OUT/overlay.${ext}" "$WORK/assets/overlay.${ext}"
        chmod -R a+rX "$WORK"
        chmod 777 "$WORK/output"

        docker run --rm \
            -v "$WORK:/work" \
            "$RUNTIME_IMAGE" \
            render-plan \
            --input /work/plan.json \
            --assets-root /work \
            --output "/work/output/overlay.mp4"

        cp "$WORK/output/overlay.mp4" "$mp4"
    fi

    # ── PASS 1: exit code 0 (nessun crash) ─────────────────────────────────
    echo "PASS exit code: 0"

    # ── PASS 2: file esiste e > 0 byte ─────────────────────────────────────
    if [[ ! -s "$mp4" ]]; then
        echo "FAIL overlay_${ext}.mp4 mancante o vuoto" >&2
        exit 1
    fi
    echo "PASS file presente: $(stat -c%s "$mp4") byte (> 0)"

    # ── PASS 3: l'immagine compare solo nell'intervallo ────────────────────
    ffmpeg -v error -y -ss 0.2 -i "$mp4" -frames:v 1 "$OUT/${ext}_f_before.png"
    ffmpeg -v error -y -ss 1.5 -i "$mp4" -frames:v 1 "$OUT/${ext}_f_image.png"
    ffmpeg -v error -y -ss 2.8 -i "$mp4" -frames:v 1 "$OUT/${ext}_f_after.png"

    IMG_AE="$(compare -metric AE "$OUT/${ext}_f_before.png" "$OUT/${ext}_f_image.png" null: 2>&1 || true)"
    AFTER_AE="$(compare -metric AE "$OUT/${ext}_f_before.png" "$OUT/${ext}_f_after.png" null: 2>&1 || true)"

    if [[ "$IMG_AE" -lt 10000 ]]; then
        echo "FAIL ${ext}: l'immagine non compare (AE sfondo→immagine = $IMG_AE)" >&2
        exit 1
    fi
    if [[ "$AFTER_AE" -gt 5000 ]]; then
        echo "FAIL ${ext}: l'immagine non esce correttamente (AE sfondo→fine = $AFTER_AE)" >&2
        exit 1
    fi
    echo "PASS ${ext}: immagine visibile solo nell'intervallo (AE in=$IMG_AE, AE out=$AFTER_AE)"

    # ── PASS 4: proporzioni corrette ───────────────────────────────────────
    # Misura l'aspect ratio della regione luminosa (overlay) renderizzata.
    # Con fit=contain una box 600x600 e un'immagine 2:1 producono una regione
    # ~600x300 (≈2.0); uno stretch (fit sbagliato) produrrebbe 600x600 (≈1.0).
    python3 - "$OUT/${ext}_f_image.png" <<'PY'
import sys
import numpy as np
from PIL import Image

im = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(int)
# Sfondo scuro (~13,13,20): l'overlay a colore pieno è molto più luminoso.
bright = im.sum(axis=2) > 180
count = int(bright.sum())
if count == 0:
    print("FAIL nessun pixel overlay trovato"); sys.exit(1)

ys, xs = np.where(bright)
w = int(xs.max() - xs.min() + 1)
h = int(ys.max() - ys.min() + 1)
aspect = w / h
print(f"regione overlay: {w}x{h} px, aspect={aspect:.3f}")

if not (1.85 <= aspect <= 2.15):
    print(f"FAIL proporzioni errate: aspect={aspect:.3f} (atteso ~2.0, uno stretch darebbe ~1.0)")
    sys.exit(1)
print(f"PASS proporzioni corrette: aspect={aspect:.3f} (~2.0, fit=contain rispettato)")
PY
}

run_case png
run_case jpg

echo ""
echo "Test 2: PASS — overlay PNG e JPG prodotti ($OUT)"
