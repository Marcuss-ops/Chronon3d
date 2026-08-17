#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# golden_scene_10s.sh — build and verify the golden content scene.
#
# Scene contract (10s / 300 frames @ 30fps):
#   1 background
#   2 important phrases
#   2 important words
#   2 images
#   3 simple animations (fade_in / soft_pop / slide_in)
#
# Verification:
#   1. overlay events ─▶ RenderPlan JSON is reproducible (byte-identical plan,
#      stable content fingerprint).
#   2. every content layer renders a non-trivial, distinct still.
#   3. bit-exact determinism: the same frame rendered twice in two fresh
#      processes produces identical bytes.
#
# The MP4 itself requires a video-enabled build (CHRONON3D_ENABLE_VIDEO=ON);
# the still + determinism checks below validate the render content regardless.
#
# Usage:
#   bash examples/script_overlay_tests/golden_scene_10s.sh
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SUITE_DIR="$ROOT/examples/script_overlay_tests"
BIN="${CHRONON3D_CLI_BIN:-$ROOT/.tmp/chronon-builds/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli}"
OUT="${CHRONON3D_TEST_OUT:-/tmp/chronon_golden_scene}"
SCENE="$SUITE_DIR/golden_scene_10s.json"

if [[ ! -x "$BIN" ]]; then
    echo "ERROR: CLI binary not found at $BIN" >&2
    exit 2
fi
mkdir -p "$OUT"
pass=0
fail=0

ok()   { printf '    PASS %s\n' "$1"; pass=$((pass + 1)); }
bad()  { printf '    FAIL %s\n' "$1"; fail=$((fail + 1)); }

# ── 1. Plan reproducibility ────────────────────────────────────────────────
echo "=== Plan reproducibility (script → RenderPlan twice) ==="
"$BIN" script "$SCENE" -o "$OUT/golden_a.plan.json" >/dev/null 2>&1
"$BIN" script "$SCENE" -o "$OUT/golden_b.plan.json" >/dev/null 2>&1
if cmp -s "$OUT/golden_a.plan.json" "$OUT/golden_b.plan.json"; then
    ok "RenderPlan JSON is byte-identical across runs"
else
    bad "RenderPlan JSON diverged across runs"
fi

# Structural contract: 7 layers (1 background + 6 content) and 3 animations.
LAYERS=$(grep -c '"id"' "$OUT/golden_a.plan.json")
ANIMS=$(grep -c '"preset": "fade_in"\|"preset": "soft_pop"\|"preset": "slide_in"' "$OUT/golden_a.plan.json")
if [[ "$LAYERS" -eq 7 ]]; then ok "7 layers present (1 background + 6 content)"; else bad "expected 7 layers, got $LAYERS"; fi
if [[ "$ANIMS" -ge 3 ]]; then ok "3 animation presets present"; else bad "expected 3 animations, got $ANIMS"; fi

PLAN="$OUT/golden_a.plan.json"

# ── 2. One still per content layer (distinct content) ─────────────────────
echo "=== Per-layer stills ==="
declare -A STILLS=(
    [phrase_01]=30
    [word_01]=80
    [image_01]=150
    [phrase_02]=210
    [word_02]=250
    [image_02]=280
)
for layer in phrase_01 word_01 image_01 phrase_02 word_02 image_02; do
    frame="${STILLS[$layer]}"
    out="$OUT/${layer}_f${frame}.png"
    if "$BIN" render-plan --input "$PLAN" --start-frame "$frame" --end-frame "$frame" \
          --assets-root "$ROOT" --output "$out" >/dev/null 2>&1; then
        if [[ -s "$out" ]] && file "$out" | grep -q 'PNG image data'; then
            ok "${layer}@${frame} rendered ($(stat -c%s "$out") bytes)"
        else
            bad "${layer}@${frame} not a PNG"
        fi
    else
        bad "${layer}@${frame} render failed"
    fi
done

# Distinct-content check: all six stills must have unique hashes.
UNIQ=$(md5sum "$OUT"/{phrase_01_f30,word_01_f80,image_01_f150,phrase_02_f210,word_02_f250,image_02_f280}.png \
        | awk '{print $1}' | sort -u | wc -l)
if [[ "$UNIQ" -eq 6 ]]; then ok "6 stills are pairwise distinct"; else bad "expected 6 distinct stills, got $UNIQ"; fi

# ── 3. Bit-exact determinism (two fresh processes) ─────────────────────────
echo "=== Determinism (same frame, two fresh processes) ==="
"$BIN" render-plan --input "$PLAN" --start-frame 150 --end-frame 150 \
    --assets-root "$ROOT" --output "$OUT/det_a.png" >/dev/null 2>&1
"$BIN" render-plan --input "$PLAN" --start-frame 150 --end-frame 150 \
    --assets-root "$ROOT" --output "$OUT/det_b.png" >/dev/null 2>&1
if cmp -s "$OUT/det_a.png" "$OUT/det_b.png"; then
    ok "frame 150 is bit-exact across two fresh processes"
else
    bad "frame 150 diverged across two fresh processes"
fi

echo ""
echo "Result: $pass PASS / $fail FAIL"
[[ "$fail" -eq 0 ]]
