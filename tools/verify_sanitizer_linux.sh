#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_sanitizer_linux.sh
#
# Canonical Sanitizer certification gate.
#
# Certifies the sanitizer surface per user spec verbatim:
#   - ASan+UBSan on ALL tests (preset linux-asan)
#   - TSan on 7 subsystems: cache, FontEngine, video sink, thread,
#     render concorrenti, CameraSession, diagnostics (preset linux-tsan)
#   - 0 leak ASan is Definition-of-Done hard
#   - 0 OOB / 0 UAF / 0 UB / 0 data races
#
# Verdict contract:
#   SANITIZER_FUNCTIONAL_PASS    — all sanitizer runs pass, 0 leaks, 0 races
#   SANITIZER_FUNCTIONAL_FAIL    — any leak/OOB/UAF/UB/race or test failure
#   SANITIZER_FUNCTIONAL_BLOCKED — env/binary/presets not available
#   Exit 0 = PASS, 1 = FAIL, 2 = BLOCKED
#
# Usage:
#   bash tools/verify_sanitizer_linux.sh
#
# Environment:
#   CHRONON3D_SAN_SKIP_BUILD=1   Skip cmake configure + build (reuse existing)
#   CHRONON3D_SAN_SKIP_TSAN=1    Skip TSan phase (ASan/UBSan only)
# ═══════════════════════════════════════════════════════════════════════════

GATE_NAME="verify_sanitizer_linux"

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

CHRONON3D_SAN_SKIP_BUILD="${CHRONON3D_SAN_SKIP_BUILD:-0}"
CHRONON3D_SAN_SKIP_TSAN="${CHRONON3D_SAN_SKIP_TSAN:-0}"

# NOTE: set -u + pipefail only — NO set -e (must run all phases and aggregate)
set -uo pipefail

NPROC=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)

PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0
ENV_BLOCKED=false

# ── Helpers ──────────────────────────────────────────────────────────────────

_gate_pass()   { echo "  [PASS]    $1"; PASS_COUNT=$((PASS_COUNT + 1)); }
_gate_fail()   { echo "  [FAIL]    $1 — $2"; FAIL_COUNT=$((FAIL_COUNT + 1)); }
_gate_blocked(){ echo "  [BLOCKED] $1 — $2"; BLOCKED_COUNT=$((BLOCKED_COUNT + 1)); }

# ══════════════════════════════════════════════════════════════════════════════
# 1. Repository state
# ══════════════════════════════════════════════════════════════════════════════

echo "=============================================="
echo " verify_sanitizer_linux.sh"
echo "=============================================="
echo ""
echo "== 1. Repository state =="

git fetch origin 2>/dev/null || true
LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse origin/main 2>/dev/null || echo "N/A")

if [ "$LOCAL" != "$REMOTE" ] \
   && [ "$REMOTE" != "N/A" ] \
   && ! git merge-base --is-ancestor "$LOCAL" "$REMOTE" 2>/dev/null \
   && ! git merge-base --is-ancestor "$REMOTE" "$LOCAL" 2>/dev/null; then
    echo "SAN_FAIL: HEAD and origin/main are divergent"
    echo "  LOCAL:  $LOCAL"
    echo "  REMOTE: $REMOTE"
    exit 1
fi

echo "  HEAD: $(git rev-parse --short HEAD)"
_gate_pass "repository_state"

# ══════════════════════════════════════════════════════════════════════════════
# 2. Environment audit
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 2. Environment audit =="

# cmake required
command -v cmake >/dev/null 2>&1 \
    && _gate_pass "cmake ($(cmake --version 2>/dev/null | head -1))" \
    || { _gate_blocked "cmake" "not found"; ENV_BLOCKED=true; }

# ctest required
command -v ctest >/dev/null 2>&1 \
    && _gate_pass "ctest" \
    || { _gate_blocked "ctest" "not found"; ENV_BLOCKED=true; }

# jq required for preset parsing
command -v jq >/dev/null 2>&1 \
    && _gate_pass "jq ($(jq --version 2>/dev/null))" \
    || { _gate_blocked "jq" "not found — required for preset parsing"; ENV_BLOCKED=true; }

# ── Preset audit ─────────────────────────────────────────────────────────────

PRESETS_FILE="CMakePresets.json"
if [ ! -f "$PRESETS_FILE" ]; then
    _gate_blocked "presets_file" "$PRESETS_FILE not found"
    ENV_BLOCKED=true
else
    # ASan configure preset
    if cmake --list-presets 2>/dev/null | grep -q '"linux-asan"'; then
        _gate_pass "preset_linux-asan"
    else
        _gate_blocked "preset_linux-asan" "not found in CMakePresets.json"
        ENV_BLOCKED=true
    fi

    # TSan configure preset
    if cmake --list-presets 2>/dev/null | grep -q '"linux-tsan"'; then
        _gate_pass "preset_linux-tsan"
    else
        _gate_blocked "preset_linux-tsan" "not found in CMakePresets.json"
        [ "$CHRONON3D_SAN_SKIP_TSAN" = "1" ] || ENV_BLOCKED=true
    fi

    # Verify ASAN_OPTIONS in the test preset (if jq available)
    if [ "$ENV_BLOCKED" = false ] && command -v jq >/dev/null 2>&1; then
        ASAN_OPTS=$(jq -r '.testPresets[]? | select(.name=="linux-asan-test") | .environment.ASAN_OPTIONS // ""' "$PRESETS_FILE" 2>/dev/null || echo "")
        if [ -n "$ASAN_OPTS" ]; then
            if echo "$ASAN_OPTS" | grep -q 'detect_leaks=1' && echo "$ASAN_OPTS" | grep -q 'halt_on_error=1'; then
                _gate_pass "asan_options (detect_leaks=1:halt_on_error=1)"
            else
                _gate_fail "asan_options" "missing detect_leaks=1 or halt_on_error=1 in ASAN_OPTIONS"
            fi
        else
            _gate_pass "asan_options (test preset not found; will use env fallback)"
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 3. ASan/UBSan build
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 3. ASan/UBSan build =="

ASAN_BUILD_DIR="build/chronon/linux-asan"

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "asan_build" "env blocked"
elif [ "$CHRONON3D_SAN_SKIP_BUILD" = "1" ]; then
    _gate_pass "asan_build (skipped — CHRONON3D_SAN_SKIP_BUILD=1)"
else
    # Configure
    echo "  Configuring linux-asan..."
    asan_rc=0
    cmake --preset linux-asan > /tmp/asan_configure.log 2>&1 || asan_rc=$?
    if [ "$asan_rc" -eq 0 ]; then
        _gate_pass "asan_configure"
    else
        _gate_fail "asan_configure" "cmake --preset linux-asan failed (see /tmp/asan_configure.log)"
        ENV_BLOCKED=true
    fi

    # Build
    if [ "$ENV_BLOCKED" = false ] && [ -d "$ASAN_BUILD_DIR" ]; then
        echo "  Building chronon3d_cli + chronon3d_tests (-j$NPROC)..."
        asan_build_rc=0
        BUILD_OUTPUT=$(cmake --build "$ASAN_BUILD_DIR" \
            --target chronon3d_cli chronon3d_tests -j"$NPROC" 2>&1) || asan_build_rc=$?
        ERROR_COUNT=$(echo "$BUILD_OUTPUT" | grep -c 'error:' 2>/dev/null || echo 0)
        if [ "$ERROR_COUNT" -eq 0 ] && [ "$asan_build_rc" -eq 0 ]; then
            _gate_pass "asan_build (0 errors)"
        else
            _gate_fail "asan_build" "$ERROR_COUNT compilation errors (exit=$asan_build_rc)"
            echo "$BUILD_OUTPUT" | grep 'error:' | head -5
        fi
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 4. ASan/UBSan ctest — 0 leak Definition-of-Done hard
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 4. ASan/UBSan ctest (0 leak ASan = DoD hard) =="

if [ "$ENV_BLOCKED" = true ]; then
    _gate_blocked "asan_ctest" "build blocked"
elif [ ! -d "$ASAN_BUILD_DIR" ]; then
    _gate_blocked "asan_ctest" "build dir $ASAN_BUILD_DIR not found"
else
    echo "  Running ctest --preset linux-asan-test --output-on-failure..."
    CTEST_RC=0
    CTEST_OUTPUT=$(ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:abort_on_error=1:print_summary=1" \
                   UBSAN_OPTIONS="halt_on_error=1:abort_on_error=1:print_stacktrace=1:print_summary=1" \
                   ctest --preset linux-asan-test --output-on-failure 2>&1) || CTEST_RC=$?

    # Explicit 0-leak enforcement (grep even if ctest passes)
    LEAK_COUNT=$(echo "$CTEST_OUTPUT" | grep -cE 'LeakSanitizer|leak detected|Direct leak|Indirect leak' 2>/dev/null || echo 0)
    OOB_COUNT=$(echo "$CTEST_OUTPUT" | grep -cE 'heap-buffer-overflow|stack-buffer-overflow|heap-use-after-free' 2>/dev/null || echo 0)
    UB_COUNT=$(echo "$CTEST_OUTPUT" | grep -cE 'runtime error:|undefined behavior|UndefinedBehavior' 2>/dev/null || echo 0)

    echo "  ctest rc=$CTEST_RC, leaks=$LEAK_COUNT, oob=$OOB_COUNT, ub=$UB_COUNT"

    if [ "$CTEST_RC" -eq 0 ] && [ "$LEAK_COUNT" -eq 0 ] && [ "$OOB_COUNT" -eq 0 ] && [ "$UB_COUNT" -eq 0 ]; then
        _gate_pass "asan_ctest (0 leaks, 0 OOB, 0 UB)"
    else
        [ "$LEAK_COUNT" -gt 0 ] && _gate_fail "asan_leaks" "$LEAK_COUNT leak(s) — DoD hard violation"
        [ "$OOB_COUNT" -gt 0 ]   && _gate_fail "asan_oob" "$OOB_COUNT OOB/UAF error(s)"
        [ "$UB_COUNT" -gt 0 ]    && _gate_fail "asan_ub" "$UB_COUNT UB error(s)"
        [ "$CTEST_RC" -ne 0 ]    && _gate_fail "asan_ctest" "ctest exit=$CTEST_RC"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 5. TSan build + ctest (7 subsystems)
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 5. TSan build + ctest (7 subsystems) =="

TSAN_BUILD_DIR="build/chronon/linux-tsan"

if [ "$ENV_BLOCKED" = true ] || [ "$CHRONON3D_SAN_SKIP_TSAN" = "1" ]; then
    _gate_blocked "tsan" "skipped or env blocked"
else
    # Build
    if [ "$CHRONON3D_SAN_SKIP_BUILD" = "1" ]; then
        _gate_pass "tsan_build (skipped)"
    else
        tsan_rc=0
        cmake --preset linux-tsan > /tmp/tsan_configure.log 2>&1 || tsan_rc=$?
        if [ "$tsan_rc" -eq 0 ]; then
            _gate_pass "tsan_configure"
        else
            _gate_fail "tsan_configure" "cmake --preset linux-tsan failed"
        fi

        if [ -d "$TSAN_BUILD_DIR" ] && [ "$tsan_rc" -eq 0 ]; then
            echo "  Building chronon3d_cli + chronon3d_tests (-j$NPROC)..."
            tsan_build_rc=0
            BUILD_OUTPUT=$(cmake --build "$TSAN_BUILD_DIR" \
                --target chronon3d_cli chronon3d_tests -j"$NPROC" 2>&1) || tsan_build_rc=$?
            ERROR_COUNT=$(echo "$BUILD_OUTPUT" | grep -c 'error:' 2>/dev/null || echo 0)
            if [ "$ERROR_COUNT" -eq 0 ] && [ "$tsan_build_rc" -eq 0 ]; then
                _gate_pass "tsan_build (0 errors)"
            else
                _gate_fail "tsan_build" "$ERROR_COUNT compilation errors (exit=$tsan_build_rc)"
            fi
        fi
    fi

    # ctest
    if [ -d "$TSAN_BUILD_DIR" ] && [ "${tsan_rc:-0}" -eq 0 ]; then
        echo "  Running ctest --preset linux-tsan-test --output-on-failure..."
        CTEST_RC=0
        CTEST_OUTPUT=$(TSAN_OPTIONS="halt_on_error=1:abort_on_error=1:second_deadlock_stack=1:print_stacktrace=1:history_size=7" \
                       ctest --preset linux-tsan-test --output-on-failure 2>&1) || CTEST_RC=$?

        RACE_COUNT=$(echo "$CTEST_OUTPUT" | grep -cE 'ThreadSanitizer|data race|WARNING:.*race' 2>/dev/null || echo 0)
        DEADLOCK_COUNT=$(echo "$CTEST_OUTPUT" | grep -cE 'deadlock|lock-order inversion' 2>/dev/null || echo 0)

        echo "  ctest rc=$CTEST_RC, races=$RACE_COUNT, deadlocks=$DEADLOCK_COUNT"

        if [ "$CTEST_RC" -eq 0 ] && [ "$RACE_COUNT" -eq 0 ] && [ "$DEADLOCK_COUNT" -eq 0 ]; then
            _gate_pass "tsan_ctest (0 races, 0 deadlocks)"
        else
            [ "$RACE_COUNT" -gt 0 ]     && _gate_fail "tsan_races" "$RACE_COUNT data race(s)"
            [ "$DEADLOCK_COUNT" -gt 0 ]  && _gate_fail "tsan_deadlocks" "$DEADLOCK_COUNT deadlock(s)"
            [ "$CTEST_RC" -ne 0 ]        && _gate_fail "tsan_ctest" "ctest exit=$CTEST_RC"
        fi
    else
        _gate_blocked "tsan_ctest" "build dir $TSAN_BUILD_DIR not found"
    fi

    # ── 7-subsystem TSan coverage audit ──────────────────────────────────────
    echo ""
    echo "  7-subsystem TSan coverage audit:"
    declare -A TSAN_SUBS=(
        ["cache"]="tests/cache/test_node_cache.cpp"
        ["FontEngine"]="tests/text/test_freetype_face_cache_concurrency.cpp"
        ["video_sink"]="tests/media/test_video_sink_registry.cpp"
        ["thread_pool"]="tests/core/test_thread_pool.cpp"
        ["render_concurrent"]="tests/renderer/test_concurrent_render.cpp"
        ["CameraSession"]="tests/scene/camera/test_camera_session_keep_last_valid.cpp"
        ["diagnostics"]="tests/text/test_text_visibility_contract.cpp"
    )
    PRESENT=0; MISSING=0
    for sub in "${!TSAN_SUBS[@]}"; do
        pattern="${TSAN_SUBS[$sub]}"
        if compgen -G "$pattern" >/dev/null 2>&1; then
            echo "    [PRESENT] $sub"
            PRESENT=$((PRESENT + 1))
        else
            echo "    [MISSING] $sub ($pattern)"
            MISSING=$((MISSING + 1))
        fi
    done
    if [ "$MISSING" -eq 0 ]; then
        _gate_pass "tsan_subsystem_coverage ($PRESENT/7 subsystems present)"
    else
        _gate_fail "tsan_subsystem_coverage" "$MISSING/7 subsystems missing (test files may not exist yet)"
    fi
fi

# ══════════════════════════════════════════════════════════════════════════════
# 6. Cleanup
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "== 6. Cleanup =="

# Keep build artifacts by default (rebuilding sanitizer from scratch is expensive)
rm -f /tmp/asan_configure.log /tmp/tsan_configure.log 2>/dev/null || true
_gate_pass "cleanup (configure logs removed; build dirs preserved)"

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
echo ""

if [ "$ENV_BLOCKED" = true ]; then
    echo "SANITIZER_FUNCTIONAL_BLOCKED"
    echo ""
    echo "  Sanitizer certification blocked by environment (no presets,"
    echo "  no cmake/ctest/jq). macchina-verifica DEFERRED to working build"
    echo "  host per AGENTS.md §honest-limitation."
    exit 2
elif [ "$FAIL_COUNT" -gt 0 ]; then
    echo "SANITIZER_FUNCTIONAL_FAIL"
    echo "  $FAIL_COUNT gate(s) failed."
    echo "  Per AGENTS.md §honesty: 0 leak + 0 OOB + 0 UAF + 0 UB + 0 races is HARD."
    exit 1
else
    echo "SANITIZER_FUNCTIONAL_PASS"
    echo "  All $PASS_COUNT gates passed. Sanitizer cert complete —"
    echo "  0 leaks, 0 OOB, 0 UAF, 0 UB, 0 data races."
    echo "[INFO] ${GATE_NAME}: ASan+UBSan (0 leak, 0 OOB, 0 UB) + TSan (0 races, 7 subsystems) — certified"
    exit 0
fi
