#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/test_golden_driver.sh — ADR-014 thin driver for the 12 user-spec
# text golden tests (chronon3d_text_golden_user_spec_tests).
#
# Conventions (AGENTS.md V0.1, ADR-014 Decision 3):
#   * No Python dependencies; no new include in include/chronon3d/; no
#     new registry/resolver/sampler/cache.
#   * Wraps the canonical cmake/ctest pipeline; does NOT replace it.
#   * Bypassing the wrapper (e.g. running ctest directly) is the canonical
#     CI path; the wrapper is a developer convenience.
#
# Usage:
#   tools/test_golden_driver.sh                       # default: linux-fast-dev, Debug
#   tools/test_golden_driver.sh linux-fast-dev
#   tools/test_golden_driver.sh linux-fast-dev Release
#   tools/test_golden_driver.sh update linux-fast-dev # CHRONON3D_UPDATE_GOLDENS=1
#
# Exit codes:
#   0   — all tests passed
#   1   — at least one test failed (or build/ctest error)
#   2   — precondition failed (e.g. no chronon3d_text_golden_user_spec_tests
#        target after build → indicates cmake config error)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

MODE="${1:-run}"
PRESET="${2:-linux-fast-dev}"
BUILD_TYPE="${3:-Debug}"
TARGET="chronon3d_text_golden_tests"
CTEST_REGEX="TextGoldenUserSpec"

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

log() { printf "[test_golden_driver] %s\n" "$*"; }
err() { printf "[test_golden_driver][ERR] %s\n" "$*" >&2; }

# ── 1. Mode dispatch ────────────────────────────────────────────────────────
case "$MODE" in
    run)   UPDATE_GOLDENS=0 ;;
    update) UPDATE_GOLDENS=1 ;;
    -h|--help|help)
        sed -n '2,20p' "$0"
        exit 0
        ;;
    *)
        err "unknown mode '$MODE' (use 'run' or 'update')"
        exit 2
        ;;
esac

# ── 2. CMake configure (idempotent) ────────────────────────────────────────
log "configure preset='$PRESET' build_type='$BUILD_TYPE'"
if ! cmake --preset "$PRESET" >/dev/null 2>&1; then
    err "cmake configure failed for preset '$PRESET'"
    err "  fix: check vcpkg + CMakePresets; re-run with --preset linux-fast-dev"
    exit 2
fi

BUILD_DIR="build/chronon/${PRESET}"
if [ ! -d "$BUILD_DIR" ]; then
    err "build dir '$BUILD_DIR' missing after configure"
    exit 2
fi

# ── 3. Build the user-spec test target ────────────────────────────────────
log "build target='$TARGET'"
if ! cmake --build "$BUILD_DIR" --target "$TARGET" -j"$(nproc)" 2>&1 | tail -20; then
    err "build failed for target '$TARGET'"
    err "  fix: see full log via 'cmake --build $BUILD_DIR -j$(nproc) 2>&1 | less'"
    exit 2
fi

# ── 4. Locate the produced binary ─────────────────────────────────────────
BINARY="$BUILD_DIR/tests/$TARGET"
if [ ! -x "$BINARY" ]; then
    err "binary '$BINARY' not found or not executable after build"
    err "  fix: verify tests/text_golden_tests.cmake registered $TARGET"
    exit 2
fi

# ── 5. Run via ctest (with optional golden-capture env) ───────────────────
log "ctest -R '$CTEST_REGEX'"
if [ "$UPDATE_GOLDENS" -eq 1 ]; then
    log "  mode: UPDATE (CHRONON3D_UPDATE_GOLDENS=1) — first-run capture"
    CHRONON3D_UPDATE_GOLDENS=1 \
        ctest --test-dir "$BUILD_DIR" -R "$CTEST_REGEX" --output-on-failure
else
    log "  mode: VERIFY (default) — regression check"
    ctest --test-dir "$BUILD_DIR" -R "$CTEST_REGEX" --output-on-failure
fi

# ── 6. Post-run hint ───────────────────────────────────────────────────────
log "artifacts: $BUILD_DIR/test_renders/golden/text/user_spec_*.png (if any)"
log "exit $?  (0=pass, 1=fail, 2=precondition)"
