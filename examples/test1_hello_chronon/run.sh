#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Test 1 — Chronon render minimo (testo → MP4 reale)
#
# Dimostra che Chronon renderizza davvero (non è uno stub): un testo
# "HELLO CHRONON" su canvas 1920x1080, durata ~3s (90 frame @ 30fps), con il
# testo visibile nell'intervallo 0.5s → 2.5s (frame 15 → 75).
#
# Di default usa il runtime reale containerizzato (lo stesso `chronon3d_cli`
# con CHRONON3D_ENABLE_VIDEO=ON che il worker impiega in produzione). Il
# build fast-dev sul host è compilato senza video exporter, quindi NON è
# adatto a questo test.
#
# Criteri PASS:
#   - exit code 0
#   - overlay.mp4 esiste e ha dimensione > 0 byte
#   - durata ≈ 3 secondi
#   - risoluzione 1920x1080
#   - il testo compare realmente (frame nell'intervallo ≠ frame di sfondo)
#
# Env:
#   CHRONON3D_RUNTIME_IMAGE  immagine runtime Chronon (default: ghcr.io/marcuss-ops/chronon3d-runtime:0.1.0)
#   CHRONON3D_CLI_BIN        se impostato, usa questo binario host invece di docker
#                            (deve essere una build con video exporter abilitato)
#   CHRONON3D_TEST_OUT       directory di output (default: /tmp/chronon_test1)
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SUITE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUNTIME_IMAGE="${CHRONON3D_RUNTIME_IMAGE:-ghcr.io/marcuss-ops/chronon3d-runtime:0.1.0}"
OUT="${CHRONON3D_TEST_OUT:-/tmp/chronon_test1}"

mkdir -p "$OUT"
MP4="$OUT/overlay.mp4"

echo "== Test 1: Chronon render minimo (HELLO CHRONON → overlay.mp4) =="

if [[ -n "${CHRONON3D_CLI_BIN:-}" ]]; then
    # Host mode: binary must be a video-enabled build.
    "$CHRONON3D_CLI_BIN" render-plan \
        --input "$SUITE_DIR/plan.json" \
        --assets-root "$ROOT" \
        --output "$MP4"
else
    # Docker mode: stage plan + font asset + writable output, then run the
    # real runtime CLI (ENTRYPOINT = chronon3d_cli) as the `chronon` user.
    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    mkdir -p "$WORK/assets/fonts" "$WORK/output"
    cp "$SUITE_DIR/plan.json" "$WORK/plan.json"
    cp "$ROOT/assets/fonts/Poppins-Bold.ttf" "$WORK/assets/fonts/"
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

# ── PASS 1: exit code 0 ────────────────────────────────────────────────────
echo "PASS exit code: 0"

# ── PASS 2: file esiste e > 0 byte ─────────────────────────────────────────
if [[ ! -s "$MP4" ]]; then
    echo "FAIL overlay.mp4 mancante o vuoto" >&2
    exit 1
fi
SIZE="$(stat -c%s "$MP4")"
echo "PASS file presente: $SIZE byte (> 0)"

# ── PASS 3+4: durata ≈ 3s e risoluzione 1920x1080 ──────────────────────────
WIDTH="$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$MP4")"
HEIGHT="$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$MP4")"
DURATION="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$MP4")"

if [[ "$WIDTH" != "1920" || "$HEIGHT" != "1080" ]]; then
    echo "FAIL risoluzione ${WIDTH}x${HEIGHT} != 1920x1080" >&2
    exit 1
fi
echo "PASS risoluzione: ${WIDTH}x${HEIGHT}"

DUR_OK="$(awk -v d="$DURATION" 'BEGIN { print (d >= 2.5 && d <= 3.5) ? "1" : "0" }')"
if [[ "$DUR_OK" != "1" ]]; then
    echo "FAIL durata ${DURATION}s fuori da ~3s" >&2
    exit 1
fi
echo "PASS durata: ${DURATION}s (≈ 3s)"

# ── PASS 5: il testo compare realmente ─────────────────────────────────────
# frame ~0.2s (sfondo, testo assente) vs frame ~1.5s (testo visibile) devono
# differire; frame ~2.8s (testo già uscito) deve tornare ~identico allo sfondo.
ffmpeg -v error -y -ss 0.2 -i "$MP4" -frames:v 1 "$OUT/f_before.png"
ffmpeg -v error -y -ss 1.5 -i "$MP4" -frames:v 1 "$OUT/f_text.png"
ffmpeg -v error -y -ss 2.8 -i "$MP4" -frames:v 1 "$OUT/f_after.png"

TEXT_AE="$(compare -metric AE "$OUT/f_before.png" "$OUT/f_text.png" null: 2>&1 || true)"
AFTER_AE="$(compare -metric AE "$OUT/f_before.png" "$OUT/f_after.png" null: 2>&1 || true)"

if [[ "$TEXT_AE" -lt 5000 ]]; then
    echo "FAIL il testo non compare (AE sfondo→testo = $TEXT_AE)" >&2
    exit 1
fi
if [[ "$AFTER_AE" -gt 5000 ]]; then
    echo "FAIL il testo non esce correttamente (AE sfondo→fine = $AFTER_AE)" >&2
    exit 1
fi
echo "PASS testo visibile nell'intervallo (AE in=$TEXT_AE, AE out=$AFTER_AE)"

echo ""
echo "Test 1: PASS — overlay.mp4 prodotto ($MP4)"
