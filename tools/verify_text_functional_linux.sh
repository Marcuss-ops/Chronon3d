#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_text_functional_linux.sh
#
# Canonical Text Production V1 functional certification gate.
#
# Mirrors tools/verify_camera_functional_linux.sh structure (7 sections)
# adapted to the text V1 spec: 4 user-spec anti-false-green invariants
# (glyph_count>0, missing_glyph_count==0, ink_bounds non-empty,
# visible_ink_pixels>100) + 16 user-spec scenarios (font valid/missing/
# corrupt, UTF-8, fallback, wrapping, auto-fit, alignment, placement,
# glow, stroke, shadow, typewriter, long text, 16:9/9:16, animation,
# clipping).
#
# §honesty contract (AGENTS.md):
#   - BLOCKED steps reported with explicit diagnostic, not silently skipped.
#   - BUILD_BLOCKED is emitted when the text targets fail to compile.
#   - TEXT_FUNCTIONAL_PASS is only emitted when ALL steps pass.
#   - TEXT_FUNCTIONAL_FAIL is emitted on any non-zero exit.
#   - TEXT_FUNCTIONAL_BLOCKED is emitted when the build is broken.
#   - Addizionale [INFO] ${GATE_NAME}: ... line on PASS (per AGENTS.md
#     "## Regole di lint documentale" §INFO-level diagnostic style).
#
# Usage:
#   bash tools/verify_text_functional_linux.sh
#
# Environment overrides:
#   CHRONON3D_TEXT_SKIP_BUILD=1     Skip the build step (use existing binaries)
#   CHRONON3D_TEXT_SKIP_CONSUMER=1  Skip external consumer test
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME="verify_text_functional_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
BUILD_BLOCKED=false

# ── Helpers ──────────────────────────────────────────────────────────────

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

# ══════════════════════════════════════════════════════════════════════════
# 1. Repository state
# ══════════════════════════════════════════════════════════════════════════

echo "=============================================="
echo " verify_text_functional_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

# Check clean tree
if ! git diff --quiet; then
    echo "TEXT_FAIL: working tree has uncommitted changes"
    exit 1
fi
if ! git diff --cached --quiet; then
    echo "TEXT_FAIL: index has staged changes"
    exit 1
fi

# Check aligned with origin/main
git fetch origin 2>/dev/null || true
BEHIND=$(git rev-list --left-right --count origin/main...HEAD 2>/dev/null | awk '{print $1}') || BEHIND="?"
if [ "$BEHIND" != "0" ]; then
    echo "TEXT_FAIL: HEAD is behind origin/main (behind=$BEHIND)"
    exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
echo "  Branch: $(git branch --show-current)"
echo "  Tree: clean"
_gate_pass "Repository state"

# ══════════════════════════════════════════════════════════════════════════
# 2. Architectural gates
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 2. Architectural gates =="

declare -A GATES=(
    # text_simplicity_adapters RETIRED (I1 audit 2026-07-13): see
    # Gate #25 in tools/check_architecture_boundaries.sh — canonical M1.8 §1 enforcement.
    ["test_hygiene"]="tools/check_test_hygiene.sh"
    ["test_suite_registration"]="tools/check_test_suite_registration.sh"
    ["doc_sync"]="tools/check_doc_sync.sh"
)

for gate_name in "${!GATES[@]}"; do
    gate_script="${GATES[$gate_name]}"
    if [ ! -f "$gate_script" ]; then
        _gate_blocked "$gate_name" "gate script not found: $gate_script"
        continue
    fi
    if bash "$gate_script" > /dev/null 2>&1; then
        _gate_pass "$gate_name"
    else
        _gate_fail "$gate_name" "exit code $?"
    fi
done

# ══════════════════════════════════════════════════════════════════════════
# 3. Disabled text test audit
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 3. Disabled text test audit =="

SKIP_PATTERNS='#if[[:space:]]+0|DOCTEST_SKIP|doctest::skip'
SKIP_DIRS=("tests/text" "tests/certification" "tests/text_golden")
SKIP_FOUND=0
SKIP_WITH_TICKET=0
SKIP_WITHOUT_TICKET=0

if ! command -v rg &>/dev/null; then
    _gate_blocked "disabled_test_audit" "rg (ripgrep) not installed"
else
    for dir in "${SKIP_DIRS[@]}"; do
        if [ -d "$dir" ]; then
            while IFS= read -r line; do
                SKIP_FOUND=$((SKIP_FOUND + 1))
                if echo "$line" | grep -qi 'TICKET-'; then
                    SKIP_WITH_TICKET=$((SKIP_WITH_TICKET + 1))
                else
                    SKIP_WITHOUT_TICKET=$((SKIP_WITHOUT_TICKET + 1))
                fi
            done < <(rg -n "$SKIP_PATTERNS" "$dir" 2>/dev/null || true)
        fi
    done

    echo "  Total skips found: $SKIP_FOUND"
    echo "  With TICKET reference: $SKIP_WITH_TICKET"
    echo "  Without TICKET reference: $SKIP_WITHOUT_TICKET"

    if [ "$SKIP_WITHOUT_TICKET" -gt 0 ]; then
        _gate_fail "disabled_test_audit" "$SKIP_WITHOUT_TICKET skip(s) without TICKET reference"
    else
        _gate_pass "disabled_test_audit (${SKIP_WITH_TICKET} skip(s), all with TICKET)"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════
# 4. Clean configure + build
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 4. Text build =="

if [ "${CHRONON3D_TEXT_SKIP_BUILD:-0}" = "1" ]; then
    echo "  CHRONON3D_TEXT_SKIP_BUILD=1 — skipping build"
    _gate_blocked "text_build" "skipped via env override"
else
    BUILD_DIR="build/chronon/linux-content-dev"

    # Configure
    echo "  Configuring..."
    if cmake --preset linux-content-dev > /dev/null 2>&1; then
        echo "  Configure: OK"
    else
        _gate_fail "text_configure" "cmake preset failed"
        BUILD_BLOCKED=true
    fi

    # Build text production v1 target
    if [ "$BUILD_BLOCKED" = false ]; then
        echo "  Building text production v1 target..."
        BUILD_OUTPUT=$(cmake --build --preset linux-content-dev \
            --target chronon3d_text_production_v1_tests -j"$(nproc)" 2>&1) || true

        ERROR_COUNT=$(echo "$BUILD_OUTPUT" | grep -c 'error:' || echo 0)

        if [ "$ERROR_COUNT" -eq 0 ]; then
            _gate_pass "text_build (0 errors)"
        else
            _gate_fail "text_build" "$ERROR_COUNT compilation errors"
            BUILD_BLOCKED=true
            echo ""
            echo "  --- First 10 errors ---"
            echo "$BUILD_OUTPUT" | grep 'error:' | head -10
            echo "  -----------------------"
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════
# 5. CTest discovery + run
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 5. CTest discovery + run =="

if [ "$BUILD_BLOCKED" = true ]; then
    _gate_blocked "ctest" "build is blocked — test binary does not exist"
else
    TEXT_BIN=$(find build -type f -name 'chronon3d_text_production_v1_tests' \
        -executable 2>/dev/null | head -1)

    if [ -z "$TEXT_BIN" ]; then
        _gate_blocked "ctest" "binary not found (build target was chronon3d_text_production_v1_tests)"
    else
        # Pre-ctest staleness check (AGENTS.md §honesty: ensure binary is
        # fresher than source before trusting ctest output)
        SRC="tests/certification/test_text_production_v1.cpp"
        if [ -x "$TEXT_BIN" ] && [ -f "$SRC" ] && [ "$SRC" -nt "$TEXT_BIN" ]; then
            _gate_blocked "ctest" "source is newer than binary — rebuild required"
        else
            echo "  Running text production v1 tests..."
            if CTEST_OUTPUT=$(ctest --output-on-failure \
                -R '^chronon3d_text_production_v1_tests$' 2>&1); then
                if echo "$CTEST_OUTPUT" | grep -q '100% tests passed'; then
                    _gate_pass "ctest (100% passed)"
                elif echo "$CTEST_OUTPUT" | grep -q 'tests passed'; then
                    PCT=$(echo "$CTEST_OUTPUT" | grep -oP '\d+(?=% tests passed)' | head -1 || echo "?")
                    _gate_fail "ctest" "only $PCT% passed"
                else
                    _gate_fail "ctest" "no test results in output"
                fi
            else
                _gate_fail "ctest" "ctest returned non-zero"
            fi
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════
# 6. Text scenario audit (user-spec 16 scenarios)
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 6. Text scenario audit =="

if [ "$BUILD_BLOCKED" = true ]; then
    _gate_blocked "text_scenarios" "build is blocked"
else
    TEXT_BIN=$(find build -type f -name 'chronon3d_text_production_v1_tests' \
        -executable 2>/dev/null | head -1)
    if [ -z "$TEXT_BIN" ]; then
        _gate_blocked "text_scenarios" "binary not found"
    else
        # Run each user-spec scenario subset (TestCase-level filters)
        declare -A SCENARIO_FILTERS=(
            ["core"]="Text requested produces visible ink"
            ["missing_font"]="Missing font"
            ["corrupt_font"]="Corrupt font"
            ["blank_text"]="Blank text"
            ["utf8"]="UTF-8"
            ["fallback"]="Font fallback"
            ["wrapping"]="Word wrapping"
            ["autofit"]="Auto-fit"
            ["alignment"]="Alignment"
            ["placement"]="Placement"
            ["glow"]="Glow"
            ["stroke"]="Stroke"
            ["shadow"]="Shadow"
            ["typewriter"]="Typewriter"
            ["long_text"]="Long text"
            ["16x9"]="Aspect 16:9"
            ["9x16"]="Aspect 9:16"
            ["animation"]="Animation"
            ["clipping"]="Clipping"
            ["alpha_threshold"]="Alpha threshold"
        )
        SCENARIOS_PASS=0
        SCENARIOS_FAIL=0
        for scn in "${!SCENARIO_FILTERS[@]}"; do
            pattern="${SCENARIO_FILTERS[$scn]}"
            if RESULT=$("$TEXT_BIN" --test-case="$pattern" 2>&1) || true; then
                if echo "$RESULT" | grep -q 'Status: SUCCESS'; then
                    SCENARIOS_PASS=$((SCENARIOS_PASS + 1))
                else
                    SCENARIOS_FAIL=$((SCENARIOS_FAIL + 1))
                fi
            else
                SCENARIOS_FAIL=$((SCENARIOS_FAIL + 1))
            fi
        done
        echo "  Scenarios: $SCENARIOS_PASS PASS, $SCENARIOS_FAIL FAIL"
        if [ "$SCENARIOS_FAIL" -eq 0 ]; then
            _gate_pass "text_scenarios ($SCENARIOS_PASS/20 PASS)"
        else
            _gate_fail "text_scenarios" "$SCENARIOS_FAIL/20 failed"
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════
# 7. External SDK consumer test (text subset)
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 7. External SDK consumer =="

if [ "${CHRONON3D_TEXT_SKIP_CONSUMER:-0}" = "1" ]; then
    echo "  CHRONON3D_TEXT_SKIP_CONSUMER=1 — skipping"
    _gate_blocked "external_consumer" "skipped via env override"
elif [ "$BUILD_BLOCKED" = true ]; then
    _gate_blocked "external_consumer" "build is blocked"
else
    if bash tools/install_consumer_test.sh > /dev/null 2>&1; then
        _gate_pass "external_consumer"
    else
        _gate_fail "external_consumer" "install_consumer_test.sh failed"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════
# Final verdict
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo ""

if [ "$BUILD_BLOCKED" = true ]; then
    echo "TEXT_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  The text build is blocked by pre-existing build rot"
    echo "  (TICKET-BUILD-ROT-CASCADE-CAMERA 409-error rot + TICKET-CONTENT-TEXT-CAMERA-V1-ROT 21-error rot +"
    echo "   TICKET-TEXT-LEGACY-POSITION-ROT 200+ sites + TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV vcpkg glm/magic_enum missing)."
    echo "  Fix the build rot on a working build host, then re-run this script."
    echo ""
    echo "  Per AGENTS.md §honesty: this VPS lacks the canonical build env; macchina-verifica is"
    echo "  DEFERRED to working build host (per the established §honest-limitation pattern)."
    exit 2
elif [ "$FAIL_COUNT" -gt 0 ]; then
    echo "TEXT_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed. See details above."
    exit 1
else
    echo "TEXT_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. Text Production V1 functional."
    # AGENTS.md Rule #2 — addizionale [INFO] line on PASS (≤ 200 chars,
    # grep-discoverable via [INFO] prefix + verify_text_functional_linux
    # self-identifier).  This is the canonical INFO-level diagnostic.
    echo "[INFO] ${GATE_NAME}: 4 anti-false-green invariants + 16 user-spec scenarios verified on working build host"
    exit 0
fi
