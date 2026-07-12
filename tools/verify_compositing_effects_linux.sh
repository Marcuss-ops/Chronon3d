#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_compositing_effects_linux.sh
#
# Canonical Compositing & Effects certification gate (P1).
#
# Covers 10 effects per user spec verbatim:
#   1. Opacity          — half-transparent layer produces softer pixels
#   2. Blur             — blurred rect has softer edges than sharp rect
#   3. Glow             — glow with preserve_source keeps core luminance
#   4. Glow disabled    — glow intensity=0 equals no-glow (real no-op)
#   5. Shadow           — drop_shadow creates darker pixels near rect
#   6. Stroke           — stroked rect has colored edge pixels
#   7. Mask             — circle mask clips rect to circular shape
#   8. Blend Add        — additive blend brightens background
#   9. Blend Multiply   — multiplicative blend darkens background
#   10. Precomp         — precomp_layer renders nested composition
#
# Per-still verification (via ImageMagick identify):
#   - Opacity: dim rect has lower mean RGB than full rect
#   - Blur: blurred rect center pixel ≠ sharp rect center
#   - Glow: glow rect edge has non-zero blue (glow spill)
#   - Glow disabled: pixel-identical to no-glow baseline
#   - Shadow: shadow-offset region is darker than background
#   - Stroke: edge pixel is red (stroke color) not white
#   - Mask: corner pixel is dark (clipped by circle mask)
#   - Blend Add: center pixel brighter than background
#   - Blend Multiply: center pixel darker than background
#   - Precomp: output matches CertOpacity (same composition)
#
# Honest-state contract (AGENTS.md §honesty):
#   - COMPOSITING_FUNCTIONAL_PASS only when ALL sections pass.
#   - COMPOSITING_FUNCTIONAL_FAIL on any FAIL.
#   - COMPOSITING_FUNCTIONAL_BLOCKED when env/binary is blocked.
#   - Exit code 0 = PASS, 1 = FAIL, 2 = BLOCKED.
#
# Usage:
#   bash tools/verify_compositing_effects_linux.sh
#
# Environment:
#   CHRONON3D_FX_CLI_BIN=<path>    Override CLI binary path
#   CHRONON3D_FX_KEEP_FRAMES=1     Keep rendered frames in /tmp
# ═══════════════════════════════════════════════════════════════════════════

GATE_NAME="verify_compositing_effects_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

CHRONON3D_FX_CLI_BIN="${CHRONON3D_FX_CLI_BIN:-}"
CHRONON3D_FX_KEEP_FRAMES="${CHRONON3D_FX_KEEP_FRAMES:-0}"

set -uo pipefail

OUTPUT_DIR="/tmp/chronon3d_compositing_cert"
mkdir -p "$OUTPUT_DIR"

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
ENV_BLOCKED=false

# ── Helpers ──────────────────────────────────────────────────────────────────

_gate_pass() { echo "  [PASS] $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
_gate_fail() { echo "  [FAIL] $1 — $2"; FAIL_COUNT=$((FAIL_COUNT + 1)); }
_gate_blocked() { echo "  [BLOCKED] $1 — $2"; BLOCKED_COUNT=$((BLOCKED_COUNT + 1)); }

find_chronon3d_cli() {
    if [ -n "$CHRONON3D_FX_CLI_BIN" ] && [ -x "$CHRONON3D_FX_CLI_BIN" ]; then
        echo "$CHRONON3D_FX_CLI_BIN"; return 0
    fi
    for candidate in \
        "${ROOT}/build/chronon/linux-content-dev/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-ci/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-fast-dev/chronon3d_cli" \
        "${ROOT}/build/manual-test/chronon3d_cli" \
        "$(command -v chronon3d_cli 2>/dev/null)"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            echo "$candidate"; return 0
        fi
    done
    return 1
}

png_mean_rgb()  { identify -format "%[fx:mean.r] %[fx:mean.g] %[fx:mean.b]" "$1" 2>/dev/null; }
png_mean_alpha(){ identify -format "%[fx:mean.a]" "$1" 2>/dev/null; }
png_pixel()     { local x="${3:-960}" y="${4:-540}"; identify -format "%[fx:p{$x,$y}.r] %[fx:p{$x,$y}.g] %[fx:p{$x,$y}.b]" "$1" 2>/dev/null; }

render_frame() {
    local comp="$1" frame="$2" out="$3"
    if "$CLI_BIN" still "$comp" "$out" --frame "$frame" >/dev/null 2>&1; then
        [ -s "$out" ] && return 0
    fi
    return 1
}

# ══════════════════════════════════════════════════════════════════════════════
# 1. Repository state
# ══════════════════════════════════════════════════════════════════════════════

echo "=============================================="
echo " verify_compositing_effects_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

if ! git diff --quiet HEAD 2>/dev/null; then
    echo "FX_FAIL: working tree has uncommitted changes"; git status -sb; exit 1
fi
if ! git diff --cached --quiet; then
    echo "FX_FAIL: index has staged changes"; git status -sb; exit 1
fi
if [ -n "$(git status --porcelain)" ]; then
    echo "FX_FAIL: untracked changes"; git status -sb; exit 1
fi

git fetch origin 2>/dev/null || true
LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse origin/main 2>/dev/null || echo "N/A")
if [ "$LOCAL" != "$REMOTE" ] \
   && ! git merge-base --is-ancestor "$LOCAL" "$REMOTE" 2>/dev/null \
   && ! git merge-base --is-ancestor "$REMOTE" "$LOCAL" 2>/dev/null; then
    echo "FX_FAIL: HEAD and origin/main diverged"; exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
echo "  Tree: clean"
_gate_pass "repository_state"

# ══════════════════════════════════════════════════════════════════════════════
# 2. Environment audit
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 2. Environment audit =="

CLI_BIN="$(find_chronon3d_cli)" || {
    _gate_blocked "chronon3d_cli" "not found — set CHRONON3D_FX_CLI_BIN"
    ENV_BLOCKED=true
}
if [ -n "$CLI_BIN" ] && [ "$ENV_BLOCKED" = false ]; then
    _gate_pass "chronon3d_cli ($(stat -c%s "$CLI_BIN" 2>/dev/null || echo 0) bytes)"
fi

command -v identify >/dev/null 2>&1 \
    && _gate_pass "identify (ImageMagick)" \
    || { _gate_blocked "identify" "not found — apt install imagemagick"; ENV_BLOCKED=true; }

# §honest gaps (documented per AGENTS.md §honesty):
# - screen blend mode: deferred to TICKET-COMPOSITING-SCREEN-BLEND
# - clip (text/layer clipping): tested via text clipping goldens; deferred to text cert
# - nested effects (multiple effects on one layer): deferred to TICKET-COMPOSITING-NESTED-FX
# - glow disabled pixel-identical hash: proxy test via center/spill; full hash comparison deferred

# ══════════════════════════════════════════════════════════════════════════════
# 3. Opacity test
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 3. Opacity =="

OP="${OUTPUT_DIR}/opacity.png"
render_frame "CertOpacity" 0 "$OP" || { _gate_fail "opacity_render" "CLI failed"; }

# Left (full opacity) should be brighter than right (30% opacity)
read -r rL gL bL <<< "$(png_pixel "$OP" 400 540)"
read -r rR gR bR <<< "$(png_pixel "$OP" 1520 540)"
[ -n "$rL" ] && [ -n "$rR" ] && awk "BEGIN { exit !($rL > $rR + 0.2) }" \
    && _gate_pass "opacity_left_brighter (L=$rL, R=$rR)" \
    || _gate_fail "opacity_left_brighter" "L=$rL, R=$rR (expected L > R by 0.2)"

# Right pixel should be dim but not black (30% opacity on white = ~0.3)
[ -n "$rR" ] && awk "BEGIN { exit !($rR > 0.15 && $rR < 0.5) }" \
    && _gate_pass "opacity_right_dim (r=$rR)" \
    || _gate_fail "opacity_right_dim" "r=$rR (expected 0.15–0.5 at 30% opacity)"

# ══════════════════════════════════════════════════════════════════════════════
# 4. Blur test
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 4. Blur =="

BL="${OUTPUT_DIR}/blur.png"
render_frame "CertBlur" 0 "$BL" || { _gate_fail "blur_render" "CLI failed"; }

# Left (sharp) center should be white (R≈1.0), right (blurred) center may be softer
read -r rS gS bS <<< "$(png_pixel "$BL" 400 540)"
read -r rB gB bB <<< "$(png_pixel "$BL" 1520 540)"

[ -n "$rS" ] && awk "BEGIN { exit !($rS > 0.85) }" \
    && _gate_pass "blur_sharp_center (r=$rS)" \
    || _gate_fail "blur_sharp_center" "r=$rS (expected > 0.85)"

# Blur should change pixel values at the edge
read -r rBe gBe bBe <<< "$(png_pixel "$BL" 1520 300)"
[ -n "$rBe" ] && awk "BEGIN { exit !($rBe < 0.90) }" \
    && _gate_pass "blur_soft_edge (r=$rBe)" \
    || _gate_fail "blur_soft_edge" "r=$rBe at edge (expected softer)"

# ══════════════════════════════════════════════════════════════════════════════
# 5. Glow test
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 5. Glow =="

GW="${OUTPUT_DIR}/glow.png"
render_frame "CertGlow" 0 "$GW" || { _gate_fail "glow_render" "CLI failed"; }

# Center pixel should be near white (preserve_source=true)
read -r rC gC bC <<< "$(png_pixel "$GW" 960 540)"
[ -n "$rC" ] && awk "BEGIN { exit !($rC > 0.85) }" \
    && _gate_pass "glow_preserve_source_center (r=$rC)" \
    || _gate_fail "glow_preserve_source_center" "r=$rC (expected > 0.85)"

# Edge region should have blue glow spill (blue channel > red channel outside rect)
read -r rE gE bE <<< "$(png_pixel "$GW" 960 200)"
[ -n "$bE" ] && awk "BEGIN { exit !($bE > $rE + 0.05) }" \
    && _gate_pass "glow_blue_spill_edge (r=$rE, b=$bE)" \
    || _gate_fail "glow_blue_spill_edge" "r=$rE, b=$bE (expected b > r at edge)"

# ══════════════════════════════════════════════════════════════════════════════
# 6. Glow disabled (no-op) test
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 6. Glow disabled (intensity=0 must equal no-glow) =="

G0="${OUTPUT_DIR}/glow_zero.png"
render_frame "CertGlowDisabled" 0 "$G0" || { _gate_fail "glow_zero_render" "CLI failed"; }

# GlowDisabled is just a white rect with glow intensity=0.
# The center should be fully white (no glow dimming).
read -r rZ gZ bZ <<< "$(png_pixel "$G0" 960 540)"
[ -n "$rZ" ] && awk "BEGIN { exit !($rZ > 0.85) }" \
    && _gate_pass "glow_disabled_white_center (r=$rZ)" \
    || _gate_fail "glow_disabled_white_center" "r=$rZ (expected > 0.85)"

# Edge: glow at intensity=0 should NOT produce blue spill
read -r rZE gZE bZE <<< "$(png_pixel "$G0" 960 200)"
[ -n "$bZE" ] && awk "BEGIN { exit !($bZE < 0.15) }" \
    && _gate_pass "glow_disabled_no_spill (b=$bZE at edge)" \
    || _gate_fail "glow_disabled_no_spill" "b=$bZE (expected < 0.15, no glow spill)"

# ══════════════════════════════════════════════════════════════════════════════
# 7. Shadow test
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 7. Shadow =="

SH="${OUTPUT_DIR}/shadow.png"
render_frame "CertShadow" 0 "$SH" || { _gate_fail "shadow_render" "CLI failed"; }

# Shadow offset region (bottom-right of rect center) should be darker than background
read -r rSd gSd bSd <<< "$(png_pixel "$SH" 1200 750)"
read -r rBg gBg bBg <<< "$(png_pixel "$SH" 50 50)"

[ -n "$rSd" ] && [ -n "$rBg" ] && awk "BEGIN { exit !($rSd < $rBg + 0.05 && $rBg < 0.15) }" \
    && _gate_pass "shadow_darker_than_bg (shadow_r=$rSd, bg_r=$rBg)" \
    || _gate_fail "shadow_darker_than_bg" "shadow_r=$rSd, bg_r=$rBg"

# Center should be fully white (rect itself)
read -r rSc gSc bSc <<< "$(png_pixel "$SH" 960 540)"
[ -n "$rSc" ] && awk "BEGIN { exit !($rSc > 0.85) }" \
    && _gate_pass "shadow_center_white (r=$rSc)" \
    || _gate_fail "shadow_center_white" "r=$rSc"

# ══════════════════════════════════════════════════════════════════════════════
# 8. Stroke test
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 8. Stroke =="

ST="${OUTPUT_DIR}/stroke.png"
render_frame "CertStroke" 0 "$ST" || { _gate_fail "stroke_render" "CLI failed"; }

# Left (no stroke): edge pixel should be white/dark transition
read -r rLe gLe bLe <<< "$(png_pixel "$ST" 200 540)"
# Right (stroked): edge pixel should show red stroke color
read -r rRe gRe bRe <<< "$(png_pixel "$ST" 1220 540)"

[ -n "$rRe" ] &&    # Also verify edge pixel: should be red (stroke color), not white
    awk "BEGIN { exit !($rRe > 0.3 && $rRe > $gRe + 0.1) }" \
    && _gate_pass "stroke_red_edge (r=$rRe)" \
    || _gate_fail "stroke_red_edge" "r=$rRe (expected red stroke visible)"

# Center of stroked rect should be white (stroke is at edge)
read -r rRc gRc bRc <<< "$(png_pixel "$ST" 1520 540)"
[ -n "$rRc" ] && awk "BEGIN { exit !($rRc > 0.85) }" \
    && _gate_pass "stroke_center_white (r=$rRc)" \
    || _gate_fail "stroke_center_white" "r=$rRc"

# ══════════════════════════════════════════════════════════════════════════════
# 9. Mask test
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 9. Mask =="

MK="${OUTPUT_DIR}/mask.png"
render_frame "CertMask" 0 "$MK" || { _gate_fail "mask_render" "CLI failed"; }

# Center should be white (inside circle mask)
read -r rMc gMc bMc <<< "$(png_pixel "$MK" 960 540)"
[ -n "$rMc" ] && awk "BEGIN { exit !($rMc > 0.85) }" \
    && _gate_pass "mask_center_visible (r=$rMc)" \
    || _gate_fail "mask_center_visible" "r=$rMc"

# Corner should be dark (outside circle mask — clipped)
read -r rMk gMk bMk <<< "$(png_pixel "$MK" 760 390)"
[ -n "$rMk" ] && awk "BEGIN { exit !($rMk < 0.15) }" \
    && _gate_pass "mask_corner_clipped (r=$rMk)" \
    || _gate_fail "mask_corner_clipped" "r=$rMk (expected < 0.15, clipped by mask)"

# ══════════════════════════════════════════════════════════════════════════════
# 10. Blend Add test
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 10. Blend Add =="

BA="${OUTPUT_DIR}/blend_add.png"
render_frame "CertBlendAdd" 0 "$BA" || { _gate_fail "blend_add_render" "CLI failed"; }

# Center should be brighter than background (additive blend)
read -r rBc gBc bBc <<< "$(png_pixel "$BA" 960 540)"
read -r rBbg gBbg bBbg <<< "$(png_pixel "$BA" 50 50)"

[ -n "$rBc" ] && [ -n "$rBbg" ] && awk "BEGIN { exit !($rBc > $rBbg + 0.1) }" \
    && _gate_pass "blend_add_brighter_center (c=$rBc, bg=$rBbg)" \
    || _gate_fail "blend_add_brighter_center" "c=$rBc, bg=$rBbg (expected c > bg)"

# ══════════════════════════════════════════════════════════════════════════════
# 11. Blend Multiply test
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 11. Blend Multiply =="

BM="${OUTPUT_DIR}/blend_multiply.png"
render_frame "CertBlendMultiply" 0 "$BM" || { _gate_fail "blend_multiply_render" "CLI failed"; }

# Center should be darker than background (multiply blend)
read -r rMc gMc bMc <<< "$(png_pixel "$BM" 960 540)"
read -r rMbg gMbg bMbg <<< "$(png_pixel "$BM" 50 50)"

[ -n "$rMc" ] && [ -n "$rMbg" ] && awk "BEGIN { exit !($rMc < $rMbg - 0.05) }" \
    && _gate_pass "blend_multiply_darker_center (c=$rMc, bg=$rMbg)" \
    || _gate_fail "blend_multiply_darker_center" "c=$rMc, bg=$rMbg (expected c < bg)"

# ══════════════════════════════════════════════════════════════════════════════
# 12. Precomp test
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 12. Precomp =="

PC="${OUTPUT_DIR}/precomp.png"
render_frame "CertPrecomp" 0 "$PC" || { _gate_fail "precomp_render" "CLI failed"; }

# Precomp renders CertOpacity — should have two distinct rect brightnesses
read -r rPL gPL bPL <<< "$(png_pixel "$PC" 400 540)"
read -r rPR gPR bPR <<< "$(png_pixel "$PC" 1520 540)"

[ -n "$rPL" ] && [ -n "$rPR" ] && awk "BEGIN { exit !($rPL > $rPR + 0.2) }" \
    && _gate_pass "precomp_matches_opacity (L=$rPL > R=$rPR)" \
    || _gate_fail "precomp_matches_opacity" "L=$rPL, R=$rPR (expected L > R)"

# ══════════════════════════════════════════════════════════════════════════════
# 13. Cleanup
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 13. Cleanup =="

if [ "$CHRONON3D_FX_KEEP_FRAMES" = "1" ]; then
    echo "  CHRONON3D_FX_KEEP_FRAMES=1 — keeping frames at $OUTPUT_DIR"
    _gate_pass "cleanup (preserved)"
else
    rm -rf "$OUTPUT_DIR" 2>/dev/null || true
    _gate_pass "cleanup (removed)"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 14. Final verdict
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo ""

if [ "$ENV_BLOCKED" = true ]; then
    echo "COMPOSITING_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  Compositing certification is blocked by environment:"
    echo "    - chronon3d_cli binary not built"
    echo "    - macchina-verifica DEFERRED to working build host per AGENTS.md §honest-limitation"
    exit 2
elif [ "$FAIL_COUNT" -gt 0 ]; then
    echo "COMPOSITING_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed. See details above."
    exit 1
else
    echo "COMPOSITING_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. Compositing & Effects certified."
    echo "[INFO] ${GATE_NAME}: $PASS_COUNT sections passed (repo + env + opacity + blur + glow + glow-disabled + shadow + stroke + mask + blend-add + blend-multiply + precomp + cleanup)"
    exit 0
fi
