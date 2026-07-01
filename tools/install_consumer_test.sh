#!/usr/bin/env bash
# tools/install_consumer_test.sh
#
# ─── End-to-end install boundary CI test for Chronon3D SDK (Phase 1.2 slim) ───
#
# Pipeline (4 phases):
#   Phase 1  inline Step 1+2  Configure + build SDK + install (or skip on FAST)
#   Phase 2  check_feature_ghosts.sh        Step 2.5 (opt-in)
#   Phase 3  check_archive_canaries.sh      Step 3 + 3.5  (boundary + canary gate)
#   Phase 4  run_external_consumer.sh       Step 4 + 5    (consumer + PNG verify)
#
# Phase scripts under tools/sdk/ declare `#!/usr/bin/env bash` and are
# invoked as `bash tools/sdk/<phase>.sh` (no executable bit required for
# CI; chmod +x granted for local-engine convenience — see tools/sdk/common.sh).
#
# Wired into CTest via `add_test(NAME install_consumer_ci COMMAND …)` in
# the top-level CMakeLists.txt, gated by the cache var
# CHRONON3D_BUILD_INSTALL_CONSUMER_TEST (enabled by the linux-ci preset).
#
# Env vars:
#   CHRONON3D_INSTALL_TEST_PRESET       default "linux-ci"
#   CHRONON3D_INSTALL_TEST_FAST         "1" skips Phase 1 (config/build/install)
#                                       and requires pre-existing
#                                       $SDK_BUILD/CMakeCache.txt +
#                                       $SDK_PREFIX/install_manifest.json
#                                       (SDK_BUILD and SDK_PREFIX must be
#                                       supplied via env — no mktemp in
#                                       FAST mode)
#   CHRONON3D_INSTALL_TEST_GHOST_SWEEP  "1" enables Phase 2 (destructive OFF
#                                       reconfigure + reinstall + ghost leak
#                                       assertion)
#
# Exit codes:
#   0 = boundary validated (all phases passed)
#   1 = boundary broken (any phase failed)
set -euo pipefail

# ── Pull helper library (idempotent guard inside common.sh) ──────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=./sdk/common.sh
source "$SCRIPT_DIR/sdk/common.sh"

# ── Env config + tool guard ──────────────────────────────────────────
: "${CHRONON3D_INSTALL_TEST_PRESET:=linux-ci}"
REPO_ROOT="${REPO_ROOT:-$(resolve_repo_root)}"
TMP_ROOT="${TMP_ROOT:-${TMPDIR:-/tmp}}"

log "=== orchestrator ==="
log "preset      : $CHRONON3D_INSTALL_TEST_PRESET"
log "repo root   : $REPO_ROOT"
log "temp root   : $TMP_ROOT"
log "fast mode   : ${CHRONON3D_INSTALL_TEST_FAST:-0}"
log "ghost sweep : ${CHRONON3D_INSTALL_TEST_GHOST_SWEEP:-0}"
require_cmake_3_27 >/dev/null
PRESET="${CHRONON3D_INSTALL_TEST_PRESET}"

# ── Phase 1: configure + build + install SDK (inline) ────────────────
# This phase MUST stay inline because SDK_BUILD and SDK_PREFIX are the
# inputs every downstream phase reads.  FAST mode skips this phase
# entirely (reuses a prior install) so a developer can iterate on Phase
# 2-4 checks without rebuilding the SDK.
#
# Fix (audit P0 #4): FAST mode now takes SDK_BUILD / SDK_PREFIX from
# the ENVIRONMENT instead of creating new mktemp dirs that overwrite
# the very dirs it's supposed to reuse.  Non-FAST mode creates fresh
# temp dirs as before.
if [[ "${CHRONON3D_INSTALL_TEST_FAST:-0}" == "1" ]]; then
    : "${SDK_BUILD:?FAST mode requires SDK_BUILD env var (pre-configured build dir)}"
    : "${SDK_PREFIX:?FAST mode requires SDK_PREFIX env var (pre-installed prefix)}"
    # NOTE: user-supplied dirs are NOT registered for cleanup in FAST
    # mode — these are pre-existing directories the user intends to
    # reuse across multiple runs.

    log "FAST mode ON: skipping Phase 1 (configure+build+install) — reusing existing SDK_BUILD=$SDK_BUILD"
    [[ -f "$SDK_BUILD/CMakeCache.txt" ]]          || fail "FAST mode requires pre-existing SDK_BUILD/CMakeCache.txt ($SDK_BUILD missing or never configured)"
    [[ -f "$SDK_PREFIX/install_manifest.json" ]]  || fail "FAST mode requires pre-existing SDK_PREFIX with a finished install"
    log "FAST preconditions satisfied: CMakeCache.txt + install_manifest.json both present"
else
    SDK_BUILD="$(mktemp_dir chronon3d_install_consumer_sdk_build)"
    SDK_PREFIX="$(mktemp_dir chronon3d_install_consumer_prefix)"
    cleanup_register "$SDK_BUILD" "$SDK_PREFIX"

    log "Phase 1.1: configure SDK (preset=$PRESET, build=$SDK_BUILD, prefix=$SDK_PREFIX)"
    cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
        -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" 1>&2

    log "Phase 1.2: build SDK target 'chronon3d_sdk_impl'"
    cmake --build "$SDK_BUILD" --target chronon3d_sdk_impl 1>&2

    log "Phase 1.3: install SDK into $SDK_PREFIX"
    cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" 1>&2
fi

# Export for phase-script consumption.
export SDK_BUILD SDK_PREFIX REPO_ROOT PRESET="$CHRONON3D_INSTALL_TEST_PRESET"
export VCPKG_INSTALLED_DIR="$REPO_ROOT/vcpkg_installed/$PRESET"
export VCPKG_TARGET_TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-linux}"

# ── Phase 2: feature-OFF ghost sweep (destructive — opt-in only) ─────
# The ghost sweep reconfigures SDK_BUILD with DIAG=OFF/CONTENT=OFF and
# reinstalls over SDK_PREFIX.  It permanently mutates the artifacts,
# so it is OFF by default.  Enable with CHRONON3D_INSTALL_TEST_GHOST_SWEEP=1
# for the full feature-OFF validation sweep CI matrix job.
if [[ "${CHRONON3D_INSTALL_TEST_GHOST_SWEEP:-0}" == "1" ]]; then
    log "Phase 2: feature-OFF ghost sweep (DIAG=OFF, CONTENT=OFF)"
    bash "$SCRIPT_DIR/sdk/check_feature_ghosts.sh"
else
    log "Phase 2: skipped (set CHRONON3D_INSTALL_TEST_GHOST_SWEEP=1 to enable)"
fi

# ── Phase 3: boundary pre-conditions + archive canary gate ───────────
log "Phase 3: archive canary gate (config + targets + archive + headers + canary symbols)"
bash "$SCRIPT_DIR/sdk/check_archive_canaries.sh"

# ── Phase 4: external consumer (configure + build + run + PNG verify) ─
log "Phase 4: external consumer (tests/install_consumer/ + [BOUNDARY-OK] + PNG)"
bash "$SCRIPT_DIR/sdk/run_external_consumer.sh"

# ── Summary (single-line JSON for CI log scraping) ───────────────────
printf '{"test":"install_consumer_ci","status":"passed","preset":"%s","fast":%s,"ghost_sweep":%s}\n' \
    "$PRESET" \
    "${CHRONON3D_INSTALL_TEST_FAST:-0}" \
    "${CHRONON3D_INSTALL_TEST_GHOST_SWEEP:-0}"
