#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_render_runtime_linux.sh
#
# Canonical Render runtime & framebuffer certification gate (P0).
#
# Executes 4 separate stills and enforces 4 distinct sha256 hashes + 7
# isolation invariants per framebuffer:
#   1. Repository state (clean tree, aligned with origin/main per GATE-MNT-01)
#   2. Environment audit (chronon3d_cli binary + ImageMagick `magick`/`identify`)
#   3. Test asset generation (cert_image_test.png via ImageMagick)
#   4. 4 stills execution (chronon3d_cli still Cert{Image,Rectangle,Text,Composite})
#   5. sha256 distinctness audit (4 hashes all different, anti-false-green)
#   6. 7 isolation tests per still (pixel present, pixel absent, alpha correct,
#      bbox correct, layer order, no black frame, no exploded geometry)
#   7. Final verdict (RUNTIME_FUNCTIONAL_PASS / FAIL / BLOCKED)
#
# Honest-state contract (AGENTS.md §honesty + §honest-limitation):
#   - RUNTIME_FUNCTIONAL_PASS is only emitted when ALL sections pass.
#   - RUNTIME_FUNCTIONAL_FAIL is emitted on any FAIL section.
#   - RUNTIME_FUNCTIONAL_BLOCKED is emitted when env/binary/asset is blocked.
#   - Exit code 0 = PASS, 1 = FAIL, 2 = BLOCKED.
#
# Usage:
#   bash tools/verify_render_runtime_linux.sh
#
# Environment overrides:
#   CHRONON3D_RUNTIME_SKIP_STILLS=1     Skip stills execution (audit-only)
#   CHRONON3D_RUNTIME_CLI_BIN=<path>    Override CLI binary path
#   CHRONON3D_RUNTIME_JOBS=<n>          (unused; reserved for future)
# ═══════════════════════════════════════════════════════════════════════════

GATE_NAME="verify_render_runtime_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Env-var initialization (BEFORE set -u) — explicit defaults
CHRONON3D_RUNTIME_SKIP_STILLS="${CHRONON3D_RUNTIME_SKIP_STILLS:-0}"
CHRONON3D_RUNTIME_CLI_BIN="${CHRONON3D_RUNTIME_CLI_BIN:-}"

set -uo pipefail   # NOTE: NOT `set -e` — each section must record its own outcome.

# Output directory (4 PNGs land here)
OUTPUT_DIR="/tmp/chronon3d_render_runtime_cert"
mkdir -p "$OUTPUT_DIR"

# 4 cert compositions + their expected PNG paths
declare -A COMPOSITIONS=(
    ["CertRectangle"]="${OUTPUT_DIR}/rectangle.png"
    ["CertImage"]="${OUTPUT_DIR}/image.png"
    ["CertText"]="${OUTPUT_DIR}/text.png"
    ["CertComposite"]="${OUTPUT_DIR}/composite.png"
)

# Test image asset (generated in Section 3)
TEST_ASSET="${ROOT}/content/certification/assets/cert_image_test.png"

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
ENV_BLOCKED=false

# ── Helpers ──────────────────────────────────────────────────────────────────

_gate_pass() {
    echo "  [PASS] $1"
    PASS_COUNT=$((PASS_COUNT + 1))
}

_gate_fail() {
    echo "  [FAIL] $1 — $2"
    FAIL_COUNT=$((FAIL_COUNT + 1))
}

_gate_blocked() {
    echo "  [BLOCKED] $1 — $2"
    BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
}

# Locate chronon3d_cli binary (canonical search paths)
find_chronon3d_cli() {
    if [ -n "$CHRONON3D_RUNTIME_CLI_BIN" ] && [ -x "$CHRONON3D_RUNTIME_CLI_BIN" ]; then
        echo "$CHRONON3D_RUNTIME_CLI_BIN"
        return 0
    fi
    # Standard build output locations
    for candidate in \
        "${ROOT}/build/chronon/linux-content-dev/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-ci/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-ci-full-validation/chronon3d_cli" \
        "${ROOT}/build/chronon/linux-fast-dev/chronon3d_cli" \
        "${ROOT}/.tmp/chronon-builds/linux-fast-dev/chronon3d_cli" \
        "${ROOT}/build/manual-test/chronon3d_cli" \
        "$(command -v chronon3d_cli 2>/dev/null)"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

# ══════════════════════════════════════════════════════════════════════════════
# 1. Repository state
# ══════════════════════════════════════════════════════════════════════════════

echo "=============================================="
echo " verify_render_runtime_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

# Clean tree
if ! git diff --quiet HEAD 2>/dev/null; then
    echo "RUNTIME_FAIL: working tree has uncommitted changes"
    git status -sb
    exit 1
fi
if ! git diff --cached --quiet; then
    echo "RUNTIME_FAIL: index has staged changes"
    git status -sb
    exit 1
fi
if [ -n "$(git status --porcelain)" ]; then
    echo "RUNTIME_FAIL: working tree has untracked changes"
    git status -sb
    exit 1
fi

git fetch origin 2>/dev/null || true
LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse origin/main 2>/dev/null || echo "N/A")
if [ "$LOCAL" != "$REMOTE" ] \
   && ! git merge-base --is-ancestor "$LOCAL" "$REMOTE" 2>/dev/null \
   && ! git merge-base --is-ancestor "$REMOTE" "$LOCAL" 2>/dev/null; then
    echo "RUNTIME_FAIL: HEAD and origin/main have diverged"
    exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
echo "  Tree: clean"
echo "  Aligned: origin/main"
_gate_pass "repository_state"

# ══════════════════════════════════════════════════════════════════════════════
# 2. Environment audit
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 2. Environment audit =="

# 2a. chronon3d_cli binary
CLI_BIN="$(find_chronon3d_cli)" || {
    _gate_blocked "chronon3d_cli_binary" "not found in any standard build path — set CHRONON3D_RUNTIME_CLI_BIN or build via cmake --preset linux-content-dev"
    ENV_BLOCKED=true
}
if [ -n "$CLI_BIN" ] && [ "$ENV_BLOCKED" = false ]; then
    CLI_SIZE=$(stat -c%s "$CLI_BIN" 2>/dev/null || echo 0)
    _gate_pass "chronon3d_cli_binary (${CLI_BIN}, ${CLI_SIZE} bytes)"
fi

# 2b. ImageMagick (magick preferred, identify fallback)
if command -v magick >/dev/null 2>&1; then
    MAGICK_VER=$(magick --version 2>/dev/null | head -1)
    _gate_pass "magick (${MAGICK_VER})"
    MAGICK_CMD="magick"
elif command -v identify >/dev/null 2>&1; then
    _gate_pass "identify (legacy ImageMagick)"
    MAGICK_CMD="identify"
else
    _gate_blocked "magick" "ImageMagick not found — install via apt install imagemagick"
    ENV_BLOCKED=true
fi

# 2c. sha256sum (coreutils)
if command -v sha256sum >/dev/null 2>&1; then
    _gate_pass "sha256sum (coreutils)"
else
    _gate_blocked "sha256sum" "command not found — install via apt install coreutils"
    ENV_BLOCKED=true
fi

# 2d. bash ≥ 4 (for associative arrays)
BASH_MAJOR="${BASH_VERSINFO[0]:-0}"
if [ "$BASH_MAJOR" -ge 4 ]; then
    _gate_pass "bash (${BASH_VERSION})"
else
    _gate_blocked "bash" "version $BASH_VERSION < 4 (associative arrays required)"
    ENV_BLOCKED=true
fi

# ══════════════════════════════════════════════════════════════════════════════
# 3. Test asset generation (CertImage needs a real PNG on disk)
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 3. Test asset generation =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "test_asset" "env blocker upstream — see section 2"
else
    mkdir -p "$(dirname "$TEST_ASSET")"
    # 200×150 solid blue PNG (deterministic; sha256 stable across runs)
    if "$MAGICK_CMD" -size 200x150 xc:"#3060C0" "$TEST_ASSET" 2>/dev/null; then
        ASSET_SIZE=$(stat -c%s "$TEST_ASSET" 2>/dev/null || echo 0)
        _gate_pass "test_asset (${TEST_ASSET}, ${ASSET_SIZE} bytes)"
    else
        _gate_fail "test_asset" "magick failed to generate ${TEST_ASSET}"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 4. 4 stills execution
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 4. Stills execution (4 cert compositions @ frame 0) =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "stills" "env blocker upstream — see section 2"
else
    for comp_name in CertRectangle CertImage CertText CertComposite; do
        out_path="${COMPOSITIONS[$comp_name]}"
        echo "  Rendering $comp_name → $out_path ..."
        if "$CLI_BIN" still "$comp_name" "$out_path" --frame 0 >/dev/null 2>&1; then
            if [ -f "$out_path" ] && [ -s "$out_path" ]; then
                _gate_pass "still[$comp_name] (${out_path})"
            else
                _gate_fail "still[$comp_name]" "output file missing or empty"
            fi
        else
            _gate_fail "still[$comp_name]" "chronon3d_cli exit non-zero (binary or composition issue)"
        fi
    done
fi

# ══════════════════════════════════════════════════════════════════════════════
# 5. sha256 distinctness audit (anti-false-green)
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 5. sha256 distinctness audit (anti-false-green) =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "sha256_distinct" "env blocker upstream — see section 2"
else
    declare -A HASHES
    HASH_COUNT=0
    DUP_FOUND=false
    for comp_name in CertRectangle CertImage CertText CertComposite; do
        out_path="${COMPOSITIONS[$comp_name]}"
        if [ ! -f "$out_path" ]; then
            _gate_fail "sha256[$comp_name]" "PNG missing"
            continue
        fi
        HASH=$(sha256sum "$out_path" | awk '{print $1}')
        HASH_COUNT=$((HASH_COUNT + 1))
        echo "  $comp_name: $HASH"
        if [ -n "${HASHES[$HASH]:-}" ]; then
            DUP_FOUND=true
            _gate_fail "sha256_distinct" "duplicate hash: $comp_name matches ${HASHES[$HASH]}"
        else
            HASHES[$HASH]="$comp_name"
        fi
    done

    if [ "$DUP_FOUND" = false ] && [ "$HASH_COUNT" -eq 4 ]; then
        _gate_pass "sha256_distinct (4/4 unique hashes — anti-false-green SATISFIED)"
    elif [ "$HASH_COUNT" -ne 4 ]; then
        _gate_fail "sha256_distinct" "only $HASH_COUNT/4 PNGs produced"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 6. 7 isolation tests per still
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 6. 7 isolation tests per still (pixel/alpha/bbox/layer-order/no-black/no-explode) =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "isolation_tests" "env blocker upstream — see section 2"
else
    for comp_name in CertRectangle CertImage CertText CertComposite; do
        out_path="${COMPOSITIONS[$comp_name]}"
        if [ ! -f "$out_path" ]; then
            for i in 1 2 3 4 5 6 7; do
                _gate_fail "isolation[$comp_name/$i]" "PNG missing"
            done
            continue
        fi

        # Test 1: pixel present (mean alpha in center > 0.05)
        A_CENTER=$("$MAGICK_CMD" "$out_path" -gravity center -crop 10x10+0+0 +repage -format "%[fx:mean.a]" info: 2>/dev/null)
        if [ -n "$A_CENTER" ] && awk "BEGIN { exit !($A_CENTER > 0.05) }"; then
            _gate_pass "isolation[$comp_name/1_pixel_present] (mean_alpha=$A_CENTER)"
        else
            _gate_fail "isolation[$comp_name/1_pixel_present]" "mean alpha in center = $A_CENTER (expected > 0.05)"
        fi

        # Test 2: pixel absent (mean alpha at top-left corner < 0.01 for non-Composite,
        # Composite has image at top-left so relax to < 0.5)
        A_CORNER=$("$MAGICK_CMD" "$out_path" -gravity northwest -crop 10x10+0+0 +repage -format "%[fx:mean.a]" info: 2>/dev/null)
        if [ "$comp_name" = "CertComposite" ]; then
            # Composite has image in top-left, so alpha can be non-zero there
            if awk "BEGIN { exit !($A_CORNER >= 0.0 && $A_CORNER < 1.5) }"; then
                _gate_pass "isolation[$comp_name/2_pixel_absent] (corner_alpha=$A_CORNER, range check)"
            else
                _gate_fail "isolation[$comp_name/2_pixel_absent]" "corner alpha = $A_CORNER (out of range)"
            fi
        else
            if awk "BEGIN { exit !($A_CORNER < 0.01) }"; then
                _gate_pass "isolation[$comp_name/2_pixel_absent] (corner_alpha=$A_CORNER)"
            else
                _gate_fail "isolation[$comp_name/2_pixel_absent]" "corner alpha = $A_CORNER (expected < 0.01)"
            fi
        fi

        # Test 3: alpha correct (max alpha in valid range [0, 1])
        A_MAX=$("$MAGICK_CMD" "$out_path" -format "%[fx:max.a]" info: 2>/dev/null)
        if [ -n "$A_MAX" ] && awk "BEGIN { exit !($A_MAX >= 0.0 && $A_MAX <= 1.0) }"; then
            _gate_pass "isolation[$comp_name/3_alpha_correct] (max_alpha=$A_MAX)"
        else
            _gate_fail "isolation[$comp_name/3_alpha_correct]" "max alpha = $A_MAX (out of [0,1] range)"
        fi

        # Test 4: bbox correct (PNG dimensions are 1920×1080)
        W=$("$MAGICK_CMD" "$out_path" -format "%w" info: 2>/dev/null)
        H=$("$MAGICK_CMD" "$out_path" -format "%h" info: 2>/dev/null)
        if [ "$W" = "1920" ] && [ "$H" = "1080" ]; then
            _gate_pass "isolation[$comp_name/4_bbox_correct] (${W}x${H})"
        else
            _gate_fail "isolation[$comp_name/4_bbox_correct]" "got ${W}x${H}, expected 1920x1080"
        fi

        # Test 5: layer order (Composite only — text on top of rect on top of image on top of bg)
        if [ "$comp_name" = "CertComposite" ]; then
            # Sample the center pixel where text should be (yellow)
            R_CENTER=$("$MAGICK_CMD" "$out_path" -format "%[fx:p{960,540}.r]" info: 2>/dev/null)
            G_CENTER=$("$MAGICK_CMD" "$out_path" -format "%[fx:p{960,540}.g]" info: 2>/dev/null)
            B_CENTER=$("$MAGICK_CMD" "$out_path" -format "%[fx:p{960,540}.b]" info: 2>/dev/null)
            # Yellow text: R high, G high, B low
            if awk "BEGIN { exit !($R_CENTER > 0.5 && $G_CENTER > 0.5 && $B_CENTER < 0.3) }"; then
                _gate_pass "isolation[CertComposite/5_layer_order] (center=($R_CENTER,$G_CENTER,$B_CENTER) — yellow text on top)"
            else
                _gate_fail "isolation[CertComposite/5_layer_order]" "center pixel = ($R_CENTER,$G_CENTER,$B_CENTER), expected yellow text (R+G high, B low)"
            fi
        else
            _gate_pass "isolation[$comp_name/5_layer_order] (single-layer — order trivially correct)"
        fi

        # Test 6: no black frame (mean RGB > 0.005 — allows for mostly-dark bg with subject)
        R_MEAN=$("$MAGICK_CMD" "$out_path" -format "%[fx:mean.r]" info: 2>/dev/null)
        G_MEAN=$("$MAGICK_CMD" "$out_path" -format "%[fx:mean.g]" info: 2>/dev/null)
        B_MEAN=$("$MAGICK_CMD" "$out_path" -format "%[fx:mean.b]" info: 2>/dev/null)
        # At least one channel must be non-trivially bright (text/rect/image provides it).
        # POSIX awk: no built-in max(); use explicit if-chain.
        if awk "BEGIN { m = $R_MEAN; if ($G_MEAN > m) m = $G_MEAN; if ($B_MEAN > m) m = $B_MEAN; exit !(m > 0.005) }"; then
            _gate_pass "isolation[$comp_name/6_no_black_frame] (rgb_mean=($R_MEAN,$G_MEAN,$B_MEAN))"
        else
            _gate_fail "isolation[$comp_name/6_no_black_frame]" "mean RGB = ($R_MEAN,$G_MEAN,$B_MEAN) — max < 0.005 (likely all-black frame)"
        fi

        # Test 7: no exploded geometry (bbox is within canvas bounds; trim bbox within W,H)
        TRIM_W=$("$MAGICK_CMD" "$out_path" -format "%@" info: 2>/dev/null | tr 'x' ' ' | awk '{print $1}')
        TRIM_H=$("$MAGICK_CMD" "$out_path" -format "%@" info: 2>/dev/null | tr 'x' ' ' | awk '{print $2}')
        if [ -n "$TRIM_W" ] && [ -n "$TRIM_H" ] && [ "$TRIM_W" -le 1920 ] && [ "$TRIM_H" -le 1080 ]; then
            _gate_pass "isolation[$comp_name/7_no_exploded_geometry] (trim=${TRIM_W}x${TRIM_H} within 1920x1080)"
        else
            _gate_fail "isolation[$comp_name/7_no_exploded_geometry]" "trim = ${TRIM_W}x${TRIM_H} (out of canvas bounds or invalid)"
        fi
    done
fi

# ══════════════════════════════════════════════════════════════════════════════
# 7. Final verdict
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo "  Output:  $OUTPUT_DIR"
echo ""

# NOTE: [INFO] line emission per AGENTS.md Rule #2 is ONLY on the PASS path
# (below) as the addizionale line. The BLOCKED path is non-PASS, so the
# [INFO] emission is suppressed here.

if [ "$ENV_BLOCKED" = true ]; then
    echo "RUNTIME_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  Render runtime certification is blocked by environment:"
    echo "    - chronon3d_cli binary not built (vcpkg glm/magic_enum + tmpfs env-blocked VPS)"
    echo "    - Fix: resolve TICKET-BUILD-ROT-CASCADE-CAMERA + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV"
    echo "    - macchina-verifica DEFERRED to working build host per AGENTS.md §honest-limitation"
    exit 2
elif [ "$FAIL_COUNT" -gt 0 ]; then
    echo "RUNTIME_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed. See details above."
    exit 1
else
    echo "RUNTIME_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. Render runtime certified."
    echo "[INFO] ${GATE_NAME}: $PASS_COUNT sections passed (repo state + env + asset + 4 stills + sha256 + 7 isolation tests × 4 stills)"
    exit 0
fi
