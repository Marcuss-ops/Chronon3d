#!/usr/bin/env bash
# ============================================================================
# check_clean_rebuild.sh — opt-in periodic gate: hard-wipe + rebuild + ctest smoke
# ============================================================================
#
# Companion to GATE-MNT-01 (the read-side triad `tools/check_main_clean.sh` +
# `tools/wrap_push.sh` + per-branch rebase).  Closes the silent-class "rotted
# artifact" failure mode that incremental ninja exits 0 ("no work to do") on
# even when the underlying headers / generation pipeline / precompile cache /
# persistent resolver cache are stale (mtime-bit-flips, transitive include
# path mutations, generator-stage changes that ninja missed, etc.).
#
# Opt-in by design: NOT in `tools/wrap_push.sh`'s standard gate chain (would
# slow every commit by 5-15 min per build dir).  Stand-alone run on a periodic
# cadence (weekly CI cron / pre-release rot-detect / after a suspicious-looking
# "no work to do" exit 0).
#
# Pipeline (per build dir, 4 steps):
#   1. WIPE       — `rm -rf` each build dir in $CHRONON3D_CLEAN_REBUILD_BUILD_DIRS
#                    (default: `build/chronon/linux-content-dev build/manual-test`)
#   2. CONFIGURE  — `cmake -S . -B <dir> -DCMAKE_BUILD_TYPE=$BUILD_TYPE` (re-discovers
#                    targets + transitive include paths + generator stage; clean
#                    state guarantees no stale CMakeCache from prior branches)
#   3. BUILD      — `cmake --build <dir> --target <target> -j$(nproc)`
#                    with $CHRONON3D_CLEAN_REBUILD_TARGET (default: `chronon3d_core_tests`)
#                    + --config "$BUILD_TYPE" on multi-config generators (Ninja
#                    Multi-Config / Visual Studio / Xcode); single-config
#                    generators (Ninja, Unix Makefiles) ignore --config gracefully.
#   4. SMOKE      — `ctest --test-dir <dir> -R <regex> --timeout 30`
#                    with $CHRONON3D_CLEAN_REBUILD_CTEST_REGEX (default: `^chronon3d_`)
#
# Opt-in semantics:
#   WHEN invoked WITHOUT CHRONON3D_CLEAN_REBUILD=1:
#     → `[INFO]` no-op diagnostic, exit 0.  This is the inverse of the
#     standard pre-push gate chain: this gate is opt-IN, not opt-OUT; the
#     env var is the explicit activation signal.  This preserves the
#     fast incremental-flow for the standard developer loop.
#   WHEN invoked WITH CHRONON3D_CLEAN_REBUILD=1:
#     → runs the full pipeline.  Destructive wipe of $BUILD_DIRS is gated
#     by the env var; the path-list is echoed before the wipe so the
#     operator can see exactly what is being destroyed (no surprise).
#
# Configurable via env vars:
#   CHRONON3D_CLEAN_REBUILD              = 1 to opt in (default: unset → no-op)
#   CHRONON3D_CLEAN_REBUILD_BUILD_DIRS   = space-separated build dirs (default below)
#   CHRONON3D_CLEAN_REBUILD_TARGET       = build target (default: chronon3d_core_tests)
#   CHRONON3D_CLEAN_REBUILD_BUILD_TYPE   = build config (default: Release; ignored on
#                                            single-config generators, applied via
#                                            --config on multi-config generators)
#   CHRONON3D_CLEAN_REBUILD_CTEST_REGEX  = ctest -R regex (default: ^chronon3d_)
#   CHRONON3D_CLEAN_REBUILD_DRY_RUN      = 1 to print commands only (default: 0)
#
# Exit codes:
#   0 = clean state (either opt-out PASS or opt-in pipeline PASS)
#   1 = rot detected (BUILD_FAIL or CTEST_FAIL)
#   2 = internal-script-error (missing dep, wipe permission, etc.)
#
# Diagnostic style (per AGENTS.md §Lint discipline Rule #2 INFO-level
# diagnostic style): `[INFO] ${GATE_NAME}:` is reserved for the canonical
# clean-state diagnostic on PASS (no-op exit + post-pipeline exit).  Step
# traces use the `STEP:` prefix so `rg '\[INFO\] check_clean_rebuild'`
# returns exactly the canonical audit signals (one per clean state).
#
# Stand-alone usage:
#   # Default no-op (just prints opt-in hint):
#   bash tools/check_clean_rebuild.sh
#
#   # Opt-in: hard-wipe + rebuild + ctest smoke for canonical preset:
#   CHRONON3D_CLEAN_REBUILD=1 bash tools/check_clean_rebuild.sh
#
#   # Opt-in: subset of build dirs:
#   CHRONON3D_CLEAN_REBUILD=1 \
#     CHRONON3D_CLEAN_REBUILD_BUILD_DIRS="build/manual-test" \
#     bash tools/check_clean_rebuild.sh
#
#   # Opt-in: Debug-mode rebuild:
#   CHRONON3D_CLEAN_REBUILD=1 \
#     CHRONON3D_CLEAN_REBUILD_BUILD_TYPE=Debug \
#     bash tools/check_clean_rebuild.sh
#
#   # Opt-in: dry-run (print commands without executing destructive step):
#   CHRONON3D_CLEAN_REBUILD=1 CHRONON3D_CLEAN_REBUILD_DRY_RUN=1 \
#     bash tools/check_clean_rebuild.sh
#
# Invoked by:
#   - CI periodic cron (manual or weekly cadence)
#   - Manual pre-release rot-detect by maintainers
#   - NOT by `tools/wrap_push.sh` (opt-in discipline preserves developer flow)
# ============================================================================

set -euo pipefail

GATE_NAME="check_clean_rebuild"

# ── Resolve repository root ────────────────────────────────────────────────
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT" || { echo "GATE_INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2; exit 2; }

# ── Defaults (configurable via env) ────────────────────────────────────────
DEFAULT_BUILD_DIRS="build/chronon/linux-content-dev build/manual-test"
DEFAULT_TARGET="chronon3d_core_tests"
DEFAULT_BUILD_TYPE="Release"
DEFAULT_CTEST_REGEX='^chronon3d_'

BUILD_DIRS="${CHRONON3D_CLEAN_REBUILD_BUILD_DIRS:-$DEFAULT_BUILD_DIRS}"
TARGET="${CHRONON3D_CLEAN_REBUILD_TARGET:-$DEFAULT_TARGET}"
BUILD_TYPE="${CHRONON3D_CLEAN_REBUILD_BUILD_TYPE:-$DEFAULT_BUILD_TYPE}"
CTEST_REGEX="${CHRONON3D_CLEAN_REBUILD_CTEST_REGEX:-$DEFAULT_CTEST_REGEX}"
DRY_RUN="${CHRONON3D_CLEAN_REBUILD_DRY_RUN:-0}"

# ── Banner ─────────────────────────────────────────────────────────────────
echo "=== ${GATE_NAME}: opt-in periodic clean-rebuild gate ==="
echo "  repo root:       $REPO_ROOT"
echo "  build dirs:      $BUILD_DIRS"
echo "  target:          $TARGET"
echo "  build type:      $BUILD_TYPE"
echo "  ctest -R regex:  $CTEST_REGEX"
echo "  dry-run:         $DRY_RUN"
echo ""

# ── Opt-in check → default behavior is NO-OP + diagnostic ──────────────────
# This is the inverse-disciplined complement to `tools/wrap_push.sh` (which
# runs the read-side triad UNCONDITIONALLY on every push).  This gate is opt-IN
# because:
#   - full clean rebuild + ctest costs 5-15 minutes per build dir
#   - incremental ninja is the default developer flow (fast + verified
#     by GATE-MNT-01 + check_main_clean.sh + post-push SHA-selfcheck)
#   - the rot class this gate catches is rare in normal turn-around (only
#     surfaces after mtime-bit-flips, header transitive mutations, or
#     generator-stage changes that incremental ninja missed)
if [ "${CHRONON3D_CLEAN_REBUILD:-0}" != "1" ]; then
    echo "[INFO] ${GATE_NAME}: opt-out (CHRONON3D_CLEAN_REBUILD != '1' — no-op)"
    echo "  hint: CHRONON3D_CLEAN_REBUILD=1 bash tools/check_clean_rebuild.sh"
    echo "  hint: invoke on periodic cadence (CI cron / pre-release); not on every-push"
    exit 0
fi

START_TS=$(date +%s)
echo "STEP: ${GATE_NAME}: opt-in active — running hard-wipe + rebuild + ctest pipeline"
echo ""

# ── Pre-flight: cmake / ctest / nproc must be on PATH ──────────────────────
command -v cmake >/dev/null 2>&1 || {
    echo "GATE_FAIL: cmake not found in PATH" >&2
    echo "  fix: install cmake or source the vcpkg/vcpkg-bootstrap environment" >&2
    exit 2; }
command -v ctest >/dev/null 2>&1 || {
    echo "GATE_FAIL: ctest not found in PATH (usually ships with cmake)" >&2
    exit 2; }
command -v nproc >/dev/null 2>&1 || {
    echo "GATE_FAIL: nproc not found in PATH" >&2
    exit 2; }

JOBS="$(nproc)"

# ── Step 1: WIPE — print path-list BEFORE destroying anything ─────────────
echo "STEP: ${GATE_NAME}: WIPE — paths to be destroyed:"
for d in $BUILD_DIRS; do
    abs="$REPO_ROOT/$d"
    if [ -d "$abs" ]; then echo "  - $abs (exists)"
    else echo "  - $abs (absent; rm will be skipped)"
    fi
done
echo ""

for d in $BUILD_DIRS; do
    abs="$REPO_ROOT/$d"
    if [ -d "$abs" ]; then
        echo "STEP: ${GATE_NAME}: rm -rf $abs"
        if [ "$DRY_RUN" != "1" ]; then
            rm -rf "$abs"
        else
            echo "  (dry-run: skipping exec)"
        fi
    fi
done
echo ""

# ── Step 2 + 3: CONFIGURE + BUILD (per build dir) ─────────────────────────
# Multi-config generator detection: parses CMAKE_GENERATOR from the freshly
# configured CMakeCache.txt.  Multi-config generators (Ninja Multi-Config,
# Visual Studio, Xcode) silently ignore -DCMAKE_BUILD_TYPE and require --config
# on `cmake --build` to select the active build type.  Single-config generators
# (Ninja, Unix Makefiles) honor -DCMAKE_BUILD_TYPE and ignore --config.
BUILD_FAIL=0
for d in $BUILD_DIRS; do
    abs="$REPO_ROOT/$d"
    echo "STEP: ${GATE_NAME}: configure + build for $abs (target=$TARGET, type=$BUILD_TYPE)"
    if [ "$DRY_RUN" != "1" ]; then
        if ! cmake -S . -B "$abs" \
            -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/cmake/Chronon3DVcpkgToolchain.cmake" \
            -DVCPKG_MANIFEST_FEATURES="tests" \
            -DCHRONON3D_BUILD_TESTS=ON \
            -G Ninja; then
            echo "GATE_FAIL: cmake configure failed for $abs" >&2
            BUILD_FAIL=1
            continue
        fi
        # Detect generator from CMakeCache to decide on --config flag.
        # Multi-config generators MUST receive --config to single-config-build;
        # single-config generators MUST NOT receive --config (errors out).
        GENERATOR="$(grep -E '^CMAKE_GENERATOR:' "$abs/CMakeCache.txt" 2>/dev/null \
            | sed -E 's/^CMAKE_GENERATOR:.*=//' | xargs)"
        case "$GENERATOR" in
            *Multi*|*'Visual Studio'*|*Xcode*) CONFIG_FLAG=(--config "$BUILD_TYPE") ;;
            *)                                  CONFIG_FLAG=() ;;
        esac
        if ! cmake --build "$abs" --target "$TARGET" -j"$JOBS" "${CONFIG_FLAG[@]}"; then
            echo "GATE_FAIL: cmake build failed for $abs (target=$TARGET)" >&2
            BUILD_FAIL=1
            continue
        fi
    else
        echo "  (dry-run: skipping exec)"
    fi
done

# ── Step 4: SMOKE-CTEST (per build dir) ────────────────────────────────────
CTEST_FAIL=0
for d in $BUILD_DIRS; do
    abs="$REPO_ROOT/$d"
    if [ ! -f "$abs/CTestTestfile.cmake" ]; then
        echo "STEP: ${GATE_NAME}: skipping ctest for $abs (no CTestTestfile.cmake)"
        continue
    fi
    echo "STEP: ${GATE_NAME}: smoke ctest for $abs (regex=$CTEST_REGEX)"
    if [ "$DRY_RUN" != "1" ]; then
        if ! ctest --test-dir "$abs" -R "$CTEST_REGEX" --timeout 30 --output-on-failure; then
            echo "GATE_FAIL: ctest failed for $abs (regex=$CTEST_REGEX)" >&2
            CTEST_FAIL=1
            continue
        fi
    else
        echo "  (dry-run: skipping exec)"
    fi
done

# ── Final verdict ──────────────────────────────────────────────────────────
END_TS=$(date +%s)
DURATION=$((END_TS - START_TS))
DIRS_COUNT=$(echo "$BUILD_DIRS" | wc -w)

if [ "$BUILD_FAIL" -ne 0 ] || [ "$CTEST_FAIL" -ne 0 ]; then
    echo ""
    echo "GATE_FAIL: build_fail=$BUILD_FAIL ctest_fail=$CTEST_FAIL (duration=${DURATION}s)"
    echo "  fix: see per-step diagnostics above for first failure site"
    exit 1
fi

echo ""
echo "GATE_PASS: clean rebuild OK across $DIRS_COUNT build dir(s) (target=$TARGET, duration=${DURATION}s)"
echo "[INFO] ${GATE_NAME}: $DIRS_COUNT dirs checked, 0 rot detected — incremental ninja + cmake cache now in sync with source tree"
exit 0
