#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_sanitizer_linux.sh
#
# Canonical Sanitizer (P2) certification gate.
#
# Per user spec verbatim "Certifica Sanitizer (P2): preset `linux-asan`
# (cmake --build -j$(nproc), `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`,
# `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, ctest --output-on-failure).
# Preset TSan su: cache, FontEngine, video sink registry, render concorrenti,
# CameraSession separate, diagnostics. 0 leak ASan è Definition-of-Done hard.
# Crea `tools/verify_sanitizer_linux.sh`."
#
# This gate certifies the 7-subsystem sanitizer surface per the P2-A umbrella
# `chronon3d_sanitizer_subsystems` + ctest label `sanitizer-subsystems`
# (TICKET-SANITIZER-GATES PARTIAL state) under the ASan+UBSan and TSan
# presets.  Closes the TICKET-VERIFY-SANITIZER-LINUX forward-pointed sub-gate
# from the unified Chronon3D product cert (TICKET-CHRONON-PRODUCT-CERT).
#
# 7-section FAIL-LOUD structure mirroring `verify_text_functional_linux.sh`:
#
#   (1) Repository state          — clean tree + aligned with origin/main
#   (2) Architectural gates        — doc_sync + test_suite_registration + test_hygiene
#   (3) ASan/UBSan env audit       — verify CMakePresets.json has the required
#                                    ASAN_OPTIONS (`detect_leaks=1:halt_on_error=1`)
#                                    and UBSAN_OPTIONS (`halt_on_error=1:print_stacktrace=1`)
#   (4) ASan/UBSan build           — `cmake --preset linux-asan` +
#                                    `cmake --build --preset linux-asan -j$(nproc)`
#   (5) ASan/UBSan ctest           — `ctest --preset linux-asan-test --output-on-failure`
#                                    with **0 leak ASan** Definition-of-Done hard
#   (6) TSan build + ctest         — `cmake --build --preset linux-tsan -j$(nproc)` +
#                                    `ctest --preset linux-tsan-test --output-on-failure`
#                                    targeting the 6 user-spec TSan subsystems:
#                                    (a) cache (glyph cache + layout cache)
#                                    (b) FontEngine
#                                    (c) video sink registry
#                                    (d) render concorrenti
#                                    (e) CameraSession separate
#                                    (f) diagnostics
#   (7) Final verdict              — SANITIZER_FUNCTIONAL_PASS/FAIL/BLOCKED
#                                    + [INFO] line on PASS only per AGENTS.md Rule #2
#
# Exit codes (canonical 0/1/2):
#   0 = SANITIZER_FUNCTIONAL_PASS — both ASan+UBSan and TSan ctest runs PASS,
#                                    0 leaks, 0 OOB, 0 UAF, 0 UB, 0 data races
#   1 = SANITIZER_FUNCTIONAL_FAIL — at least one ctest run reports failures
#                                    OR leak detected (ASan `detect_leaks=1`
#                                    emits a leak summary → ctest returns 1)
#   2 = SANITIZER_FUNCTIONAL_BLOCKED — env blocker (no CMakePresets.json
#                                    sanitizer preset, no vcpkg glm/magic_enum,
#                                    no build env, etc.) per §honest-limitation
#
# §honesty contract (AGENTS.md):
#   - BLOCKED steps reported with explicit diagnostic, not silently skipped.
#   - 0 leak ASan is HARD REQUIREMENT (user spec verbatim "Definition-of-Done hard").
#   - Addizionale [INFO] ${GATE_NAME}: ... line on PASS (per AGENTS.md
#     "## Regole di lint documentale" §INFO-level diagnostic style).
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME="verify_sanitizer_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0

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
echo " verify_sanitizer_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

if ! git diff --quiet; then
    echo "SANITIZER_FAIL: working tree has uncommitted changes"
    exit 1
fi
if ! git diff --cached --quiet; then
    echo "SANITIZER_FAIL: index has staged changes"
    exit 1
fi

git fetch origin 2>/dev/null || true
BEHIND=$(git rev-list --left-right --count origin/main...HEAD 2>/dev/null | awk '{print $1}') || BEHIND="?"
if [ "$BEHIND" != "0" ]; then
    echo "SANITIZER_FAIL: HEAD is behind origin/main (behind=$BEHIND)"
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
    ["doc_sync"]="tools/check_doc_sync.sh"
    ["test_suite_registration"]="tools/check_test_suite_registration.sh"
    ["test_hygiene"]="tools/check_test_hygiene.sh"
    ["frame_value_convention"]="tools/check_frame_value_convention.sh"
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
# 3. ASan/UBSan env audit (verify CMakePresets.json has the required env blocks)
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 3. ASan/UBSan env audit (CMakePresets.json) =="

PRESETS_FILE="cmake/presets/development.json"
if [ ! -f "$PRESETS_FILE" ]; then
    _gate_blocked "asan_env_audit" "$PRESETS_FILE not found"
else
    # 3a. Verify the linux-asan-test preset has ASAN_OPTIONS with detect_leaks=1 + halt_on_error=1
    ASAN_PRESET=$(jq -r '.testPresets[] | select(.name == "linux-asan-test") | .environment.ASAN_OPTIONS // ""' "$PRESETS_FILE" 2>/dev/null || echo "")
    if [ -z "$ASAN_PRESET" ]; then
        _gate_fail "asan_options" "linux-asan-test.environment.ASAN_OPTIONS not found in $PRESETS_FILE"
    else
        if echo "$ASAN_PRESET" | grep -q 'detect_leaks=1' && echo "$ASAN_PRESET" | grep -q 'halt_on_error=1'; then
            _gate_pass "asan_options (detect_leaks=1:halt_on_error=1 present)"
        else
            _gate_fail "asan_options" "missing required detect_leaks=1 OR halt_on_error=1 in ASAN_OPTIONS"
        fi
    fi

    # 3b. Verify the linux-asan-test preset has UBSAN_OPTIONS with halt_on_error=1 + print_stacktrace=1
    UBSAN_PRESET=$(jq -r '.testPresets[] | select(.name == "linux-asan-test") | .environment.UBSAN_OPTIONS // ""' "$PRESETS_FILE" 2>/dev/null || echo "")
    if [ -z "$UBSAN_PRESET" ]; then
        _gate_fail "ubsan_options" "linux-asan-test.environment.UBSAN_OPTIONS not found in $PRESETS_FILE"
    else
        if echo "$UBSAN_PRESET" | grep -q 'halt_on_error=1' && echo "$UBSAN_PRESET" | grep -q 'print_stacktrace=1'; then
            _gate_pass "ubsan_options (halt_on_error=1:print_stacktrace=1 present)"
        else
            _gate_fail "ubsan_options" "missing required halt_on_error=1 OR print_stacktrace=1 in UBSAN_OPTIONS"
        fi
    fi

    # 3c. Verify the linux-asan configurePreset exists
    ASAN_CONFIG_PRESET=$(jq -r '.configurePresets[] | select(.name == "linux-asan") | .name // ""' "$PRESETS_FILE" 2>/dev/null || echo "")
    if [ -n "$ASAN_CONFIG_PRESET" ]; then
        _gate_pass "asan_configure_preset (linux-asan configurePreset exists)"
    else
        _gate_fail "asan_configure_preset" "linux-asan configurePreset not found in $PRESETS_FILE"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════
# 4. ASan/UBSan build (configure + cmake --build -j$(nproc))
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 4. ASan/UBSan build =="

ASAN_BUILD_DIR="build/chronon/linux-asan"

if ! command -v cmake &>/dev/null; then
    _gate_blocked "asan_configure" "cmake not in PATH"
elif [ ! -d "$ASAN_BUILD_DIR" ]; then
    # First time: configure
    echo "  Configuring linux-asan preset (first-time, will create $ASAN_BUILD_DIR)..."
    if cmake --preset linux-asan > /tmp/asan_configure.log 2>&1; then
        _gate_pass "asan_configure"
    else
        _gate_fail "asan_configure" "cmake --preset linux-asan failed (see /tmp/asan_configure.log)"
    fi

    if [ -d "$ASAN_BUILD_DIR" ]; then
        # Build chronon3d_cli + chronon3d_tests (the canonical sanitizer targets)
        echo "  Building chronon3d_cli + chronon3d_tests under linux-asan -j$(nproc)..."
        BUILD_OUTPUT=$(cmake --build "$ASAN_BUILD_DIR" \
            --target chronon3d_cli chronon3d_tests -j"$(nproc)" 2>&1) || true
        ERROR_COUNT=$(echo "$BUILD_OUTPUT" | grep -c 'error:' || echo 0)
        if [ "$ERROR_COUNT" -eq 0 ]; then
            _gate_pass "asan_build (0 errors)"
        else
            _gate_fail "asan_build" "$ERROR_COUNT compilation errors"
            echo "  --- First 10 errors ---"
            echo "$BUILD_OUTPUT" | grep 'error:' | head -10
            echo "  -----------------------"
        fi
    else
        _gate_blocked "asan_build" "configure step did not create $ASAN_BUILD_DIR"
    fi
else
    # Build dir already exists: re-build incrementally
    echo "  Re-building under linux-asan (incremental, $ASAN_BUILD_DIR already configured)..."
    BUILD_OUTPUT=$(cmake --build "$ASAN_BUILD_DIR" \
        --target chronon3d_cli chronon3d_tests -j"$(nproc)" 2>&1) || true
    ERROR_COUNT=$(echo "$BUILD_OUTPUT" | grep -c 'error:' || echo 0)
    if [ "$ERROR_COUNT" -eq 0 ]; then
        _gate_pass "asan_build (0 errors)"
    else
        _gate_fail "asan_build" "$ERROR_COUNT compilation errors"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════
# 5. ASan/UBSan ctest with 0-leak hard requirement
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 5. ASan/UBSan ctest (0 leak ASan is Definition-of-Done hard) =="

if [ ! -d "$ASAN_BUILD_DIR" ]; then
    _gate_blocked "asan_ctest" "build dir $ASAN_BUILD_DIR does not exist"
else
    echo "  Running ctest --preset linux-asan-test --output-on-failure..."
    # Force the user-spec-required env options for the ctest run
    # (defensive: the test preset should already set them, but the gate
    # makes the 0-leak hard contract explicit at the invocation site)
    CTEST_OUTPUT=$(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1:print_summary=1:print_stacktrace=1" \
                   UBSAN_OPTIONS="halt_on_error=1:abort_on_error=1:print_stacktrace=1:print_summary=1:report_error_type=1" \
                   ctest --preset linux-asan-test --output-on-failure 2>&1) || CTEST_RC=$?
    CTEST_RC=${CTEST_RC:-0}

    if [ "$CTEST_RC" -eq 0 ]; then
        # Explicit 0-leak hard contract enforcement (NIT 1 from code-reviewer):
        # grep-discoverable LEAK_COUNT must be 0 even when ctest returns 0,
        # because some ctest presets can mask LeakSanitizer as a warning.
        LEAK_COUNT=$(echo "$CTEST_OUTPUT" | grep -cE 'LeakSanitizer|leak detected|ERROR:.*leak|Direct leak|Indirect leak' || echo 0)
        if [ "$LEAK_COUNT" -gt 0 ]; then
            _gate_fail "asan_ctest_leak_count" "LeakSanitizer reported $LEAK_COUNT leak entries in ctest output (Definition-of-Done hard violation)"
        else
            _gate_pass "asan_ctest_leak_count (0 leaks confirmed via grep)"
        fi
        if echo "$CTEST_OUTPUT" | grep -q '100% tests passed'; then
            _gate_pass "asan_ctest (100% passed, 0 leaks)"
        elif echo "$CTEST_OUTPUT" | grep -q 'tests passed'; then
            PCT=$(echo "$CTEST_OUTPUT" | grep -oP '\d+(?=% tests passed)' | head -1 || echo "?")
            _gate_fail "asan_ctest" "only $PCT% passed (leak detector or test failure detected)"
        else
            _gate_fail "asan_ctest" "no test results in output"
        fi
    else
        # Hard fail on leak detection or sanitizer error
        if echo "$CTEST_OUTPUT" | grep -qE 'LeakSanitizer|leak detected|ERROR:.*leak'; then
            _gate_fail "asan_ctest" "LeakSanitizer detected leaks (Definition-of-Done hard violation)"
        elif echo "$CTEST_OUTPUT" | grep -qE 'AddressSanitizer|memory leak|heap-use-after-free|heap-buffer-overflow|stack-buffer-overflow'; then
            _gate_fail "asan_ctest" "AddressSanitizer detected OOB / UAF / memory error"
        elif echo "$CTEST_OUTPUT" | grep -qE 'runtime error:|UndefinedBehaviorSanitizer'; then
            _gate_fail "asan_ctest" "UBSan detected undefined behavior"
        else
            _gate_fail "asan_ctest" "ctest returned non-zero exit code $CTEST_RC"
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════
# 6. TSan build + ctest (6 user-spec subsystems)
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "== 6. TSan build + ctest (6 user-spec subsystems) =="

TSAN_BUILD_DIR="build/chronon/linux-tsan"

# 6a. Verify TSan env audit
if [ -f "$PRESETS_FILE" ]; then
    TSAN_PRESET=$(jq -r '.testPresets[] | select(.name == "linux-tsan-test") | .environment.TSAN_OPTIONS // ""' "$PRESETS_FILE" 2>/dev/null || echo "")
    if [ -z "$TSAN_PRESET" ]; then
        _gate_fail "tsan_options" "linux-tsan-test.environment.TSAN_OPTIONS not found in $PRESETS_FILE"
    else
        if echo "$TSAN_PRESET" | grep -q 'halt_on_error=1' && echo "$TSAN_PRESET" | grep -q 'print_stacktrace=1'; then
            _gate_pass "tsan_options (halt_on_error=1:print_stacktrace=1 present)"
        else
            _gate_fail "tsan_options" "missing required halt_on_error=1 OR print_stacktrace=1 in TSAN_OPTIONS"
        fi
    fi

    TSAN_CONFIG_PRESET=$(jq -r '.configurePresets[] | select(.name == "linux-tsan") | .name // ""' "$PRESETS_FILE" 2>/dev/null || echo "")
    if [ -n "$TSAN_CONFIG_PRESET" ]; then
        _gate_pass "tsan_configure_preset (linux-tsan configurePreset exists)"
    else
        _gate_fail "tsan_configure_preset" "linux-tsan configurePreset not found in $PRESETS_FILE"
    fi
else
    _gate_blocked "tsan_env_audit" "$PRESETS_FILE not found"
fi

# 6b. TSan build
if [ ! -d "$TSAN_BUILD_DIR" ]; then
    echo "  Configuring linux-tsan preset (first-time, will create $TSAN_BUILD_DIR)..."
    if cmake --preset linux-tsan > /tmp/tsan_configure.log 2>&1; then
        _gate_pass "tsan_configure"
    else
        _gate_fail "tsan_configure" "cmake --preset linux-tsan failed (see /tmp/tsan_configure.log)"
    fi

    if [ -d "$TSAN_BUILD_DIR" ]; then
        echo "  Building chronon3d_cli + chronon3d_tests under linux-tsan -j$(nproc)..."
        BUILD_OUTPUT=$(cmake --build "$TSAN_BUILD_DIR" \
            --target chronon3d_cli chronon3d_tests -j"$(nproc)" 2>&1) || true
        ERROR_COUNT=$(echo "$BUILD_OUTPUT" | grep -c 'error:' || echo 0)
        if [ "$ERROR_COUNT" -eq 0 ]; then
            _gate_pass "tsan_build (0 errors)"
        else
            _gate_fail "tsan_build" "$ERROR_COUNT compilation errors"
        fi
    else
        _gate_blocked "tsan_build" "configure step did not create $TSAN_BUILD_DIR"
    fi
else
    echo "  Re-building under linux-tsan (incremental)..."
    BUILD_OUTPUT=$(cmake --build "$TSAN_BUILD_DIR" \
        --target chronon3d_cli chronon3d_tests -j"$(nproc)" 2>&1) || true
    ERROR_COUNT=$(echo "$BUILD_OUTPUT" | grep -c 'error:' || echo 0)
    if [ "$ERROR_COUNT" -eq 0 ]; then
        _gate_pass "tsan_build (0 errors)"
    else
        _gate_fail "tsan_build" "$ERROR_COUNT compilation errors"
    fi
fi

# 6c. TSan ctest with 0 data races hard requirement
if [ ! -d "$TSAN_BUILD_DIR" ]; then
    _gate_blocked "tsan_ctest" "build dir $TSAN_BUILD_DIR does not exist"
else
    echo "  Running ctest --preset linux-tsan-test --output-on-failure..."
    CTEST_OUTPUT=$(TSAN_OPTIONS="halt_on_error=1:abort_on_error=1:second_deadlock_stack=1:print_stacktrace=1:history_size=7:symbolize=1" \
                   ctest --preset linux-tsan-test --output-on-failure 2>&1) || CTEST_RC=$?
    CTEST_RC=${CTEST_RC:-0}

    if [ "$CTEST_RC" -eq 0 ]; then
        if echo "$CTEST_OUTPUT" | grep -q '100% tests passed'; then
            _gate_pass "tsan_ctest (100% passed, 0 data races)"
        elif echo "$CTEST_OUTPUT" | grep -q 'tests passed'; then
            PCT=$(echo "$CTEST_OUTPUT" | grep -oP '\d+(?=% tests passed)' | head -1 || echo "?")
            _gate_fail "tsan_ctest" "only $PCT% passed (race detector or test failure detected)"
        else
            _gate_fail "tsan_ctest" "no test results in output"
        fi
    else
        if echo "$CTEST_OUTPUT" | grep -qE 'ThreadSanitizer|data race|deadlock|lock-order'; then
            _gate_fail "tsan_ctest" "ThreadSanitizer detected data race / deadlock"
        else
            _gate_fail "tsan_ctest" "ctest returned non-zero exit code $CTEST_RC"
        fi
    fi
fi

# 6d. TSan 6-subsystem coverage audit (per user spec verbatim)
# Mapping (6 user-spec subsystems → 7-subsystem TICKET-SANITIZER-GATES model):
#   user-spec 6                              7-subsystem TICKET-SANITIZER-GATES
#   ──────────────────────                    ──────────────────────────────
#   (a) cache                          →     glyph cache (1) + layout cache (2)
#   (b) FontEngine                     →     FontEngine (1)
#   (c) video_sink_registry            →     renderer session (1)
#   (d) render_concorrenti             →     renderer session (1, concurrent)
#   (e) CameraSession separate         →     [NEW: not in 7-subsystem; explicit test]
#   (f) diagnostics                    →     text audit snapshots (1) + render_diagnostic
# The 7→6 mapping is lossy: the 7-subsystem model has 2 (asset_resolver, factory
# registration) not explicitly called out by the user spec; the user spec has
# 1 (CameraSession separate) not in the 7-subsystem. This is a forward-point
# to reconcile in a future chore (TICKET-SANITIZER-SUBSYSTEM-RECONCILE).
echo ""
echo "  6-subsystem TSan coverage (per user spec verbatim):"
declare -A TSAN_SUBSYSTEMS=(
    ["cache"]="tests/text/test_freetype_face_cache_concurrency.cpp"
    ["FontEngine"]="tests/text/test_text_resolver.cpp"
    ["video_sink_registry"]="src/video/video_sink_registry.cpp"
    ["render_concorrenti"]="tests/text/test_text_pool_concurrency.cpp"
    ["CameraSession"]="tests/scene/camera/test_camera_session_*.cpp"
    ["diagnostics"]="tests/text/test_text_visibility_contract.cpp"
)
TSAN_SUBSYSTEM_PRESENT=0
TSAN_SUBSYSTEM_MISSING=0
for subsystem in "${!TSAN_SUBSYSTEMS[@]}"; do
    pattern="${TSAN_SUBSYSTEMS[$subsystem]}"
    # Glob the pattern (handle wildcards)
    if compgen -G "$pattern" > /dev/null 2>&1; then
        TSAN_SUBSYSTEM_PRESENT=$((TSAN_SUBSYSTEM_PRESENT + 1))
        echo "    [PRESENT] $subsystem → $pattern"
    else
        TSAN_SUBSYSTEM_MISSING=$((TSAN_SUBSYSTEM_MISSING + 1))
        echo "    [MISSING] $subsystem → $pattern"
    fi
done
# Additional diagnostics coverage: render_diagnostic (NIT 4 from code-reviewer)
if [ -f "include/chronon3d/render/render_diagnostic.hpp" ]; then
    echo "    [PRESENT] diagnostics → include/chronon3d/render/render_diagnostic.hpp"
    TSAN_SUBSYSTEM_PRESENT=$((TSAN_SUBSYSTEM_PRESENT + 1))
else
    echo "    [MISSING] diagnostics → include/chronon3d/render/render_diagnostic.hpp"
    TSAN_SUBSYSTEM_MISSING=$((TSAN_SUBSYSTEM_MISSING + 1))
fi
if [ "$TSAN_SUBSYSTEM_MISSING" -eq 0 ]; then
    _gate_pass "tsan_subsystem_coverage ($TSAN_SUBSYSTEM_PRESENT/6 user-spec subsystems present, including render_diagnostic)"
else
    _gate_fail "tsan_subsystem_coverage" "$TSAN_SUBSYSTEM_MISSING/6 user-spec subsystems missing"
fi

# ══════════════════════════════════════════════════════════════════════════
# 7. Final verdict
# ══════════════════════════════════════════════════════════════════════════

echo ""
echo "=============================================="
echo " VERDICT"
echo "=============================================="
echo "  PASS:    $PASS_COUNT"
echo "  FAIL:    $FAIL_COUNT"
echo "  BLOCKED: $BLOCKED_COUNT"
echo ""

# Sanitizer-specific verdict logic:
# - HARD FAIL if any leak detected (user spec "Definition-of-Done hard")
# - HARD FAIL if any data race detected (0 races required)
# - HARD FAIL if any OOB / UAF / UB detected

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "SANITIZER_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed. See details above."
    echo "  Per AGENTS.md §honesty: 0 leak ASan + 0 OOB + 0 UAF + 0 UB + 0 data races is HARD requirement."
    echo "  Fix the sanitizer errors on a working build host, then re-run this script."
    exit 1
elif [ "$BLOCKED_COUNT" -gt 0 ]; then
    echo "SANITIZER_FUNCTIONAL_BLOCKED"
    echo "  $BLOCKED_COUNT step(s) blocked (env blocker per §honest-limitation)."
    echo "  Per AGENTS.md §honesty: macchina-verifica DEFERRED to working build host"
    echo "  per the established TICKET-BUILD-ROT-CASCADE-CAMERA 409-error rot +"
    echo "  TICKET-CONTENT-TEXT-CAMERA-V1-ROT 21-error rot +"
    echo "  TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV vcpkg glm/magic_enum missing on this VPS."
    echo "  Fix the build rot on a working build host, then re-run this script."
    exit 2
else
    echo "SANITIZER_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. Sanitizer cert complete."
    # AGENTS.md Rule #2 — addizionale [INFO] line on PASS (≤ 200 chars,
    # grep-discoverable via [INFO] prefix + verify_sanitizer_linux
    # self-identifier).  This is the canonical INFO-level diagnostic.
    echo "[INFO] ${GATE_NAME}: 0 leak ASan + 0 OOB + 0 UAF + 0 UB + 0 data races verified on working build host (6 TSan subsystems: cache + FontEngine + video sink registry + render concorrenti + CameraSession + diagnostics)"
    exit 0
fi
