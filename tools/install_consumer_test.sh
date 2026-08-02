#!/usr/bin/env bash
# tools/install_consumer_test.sh
#
# End-to-end install boundary CI test for Chronon3D SDK.
#
# Pipeline:
#   Step 0  bootstrap branch.main.rebase invariant
#   Phase 1 configure + build SDK + install (or skip on FAST)
#   Phase 2 check_feature_ghosts.sh (opt-in)
#   Phase 3 check_archive_canaries.sh
#   Phase 4 run_external_consumer.sh
#   Phase 5 run_asset_authoring_consumer.sh
#
# Wired into CTest through CHRONON3D_BUILD_INSTALL_CONSUMER_TEST.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=./sdk/common.sh
source "$SCRIPT_DIR/sdk/common.sh"

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

# Step 0: consumer-side forward-only rebase invariant.
if ! git config --local --get branch.main.rebase 2>/dev/null >/dev/null; then
    log "Step 0 (GATE-MNT-01-EXT): setting branch.main.rebase=true"
    git config branch.main.rebase true
fi

# Phase 1: configure + build + install SDK.
if [[ "${CHRONON3D_INSTALL_TEST_FAST:-0}" == "1" ]]; then
    : "${SDK_BUILD:?FAST mode requires SDK_BUILD env var}"
    : "${SDK_PREFIX:?FAST mode requires SDK_PREFIX env var}"

    log "FAST mode ON: reusing SDK_BUILD=$SDK_BUILD"
    [[ -f "$SDK_BUILD/CMakeCache.txt" ]] \
        || fail "FAST mode requires SDK_BUILD/CMakeCache.txt"

    # The main validation build produces the SDK archive but does not run the
    # install step. Materialize the prefix once, incrementally, so FAST mode
    # remains a real installed-consumer test instead of failing before the
    # consumer is even configured.
    SDK_ARCHIVE="$SDK_BUILD/src/libchronon3d_sdk_impl.a"
    SDK_INSTALLED_ARCHIVE="$SDK_PREFIX/lib/libchronon3d_sdk_impl.a"
    SDK_BUILD_CONFIG="$SDK_BUILD/Chronon3DConfig.cmake"
    SDK_INSTALLED_CONFIG="$SDK_PREFIX/lib/cmake/Chronon3D/Chronon3DConfig.cmake"
    if [[ ! -f "$SDK_INSTALLED_CONFIG" \
        || ! -f "$SDK_INSTALLED_ARCHIVE" \
        || ( -f "$SDK_ARCHIVE" && "$SDK_ARCHIVE" -nt "$SDK_INSTALLED_ARCHIVE" ) \
        || ( -f "$SDK_BUILD_CONFIG" && "$SDK_BUILD_CONFIG" -nt "$SDK_INSTALLED_CONFIG" ) ]]; then
        log "FAST mode: installing SDK into $SDK_PREFIX"
        cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" 1>&2
    fi
    [[ -f "$SDK_INSTALLED_CONFIG" ]] \
        || fail "FAST mode requires an installed Chronon3D package config"
    [[ -f "$SDK_INSTALLED_ARCHIVE" ]] \
        || fail "FAST mode requires the installed SDK archive"
else
    SDK_BUILD="$(mktemp_dir chronon3d_install_consumer_sdk_build)"
    SDK_PREFIX="$(mktemp_dir chronon3d_install_consumer_prefix)"
    cleanup_register "$SDK_BUILD" "$SDK_PREFIX"

    log "Phase 1.1: configure SDK (preset=$PRESET)"
    cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
        -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" 1>&2

    log "Phase 1.2: build SDK target chronon3d_sdk_impl"
    cmake --build "$SDK_BUILD" --target chronon3d_sdk_impl 1>&2

    log "Phase 1.3: install SDK into $SDK_PREFIX"
    cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" 1>&2
fi

# The consumer CMake project is configured from a separate temporary
# directory.  FAST-mode callers commonly provide repository-relative paths;
# exporting those paths verbatim makes find_package(Chronon3D) resolve them
# relative to the consumer directory and report a misleading missing-package
# error.  Freeze both paths before crossing the install boundary.
SDK_BUILD="$(cd "$SDK_BUILD" && pwd)"
mkdir -p "$SDK_PREFIX"
SDK_PREFIX="$(cd "$SDK_PREFIX" && pwd)"

# In FAST mode the caller may reuse a build produced by a different preset.
# Use the dependency root recorded by that build before falling back to the
# preset-derived path; otherwise an installed SDK built with telemetry can
# advertise unofficial::sqlite3 while the consumer is pointed at a vcpkg
# tree that never installed the telemetry feature.
if [[ -z "${VCPKG_INSTALLED_DIR:-}" && -f "$SDK_BUILD/CMakeCache.txt" ]]; then
    SDK_VCPKG_INSTALLED_DIR="$(sed -n 's/^VCPKG_INSTALLED_DIR:.*=//p' "$SDK_BUILD/CMakeCache.txt" | head -n 1)"
    if [[ -n "$SDK_VCPKG_INSTALLED_DIR" && -d "$SDK_VCPKG_INSTALLED_DIR" ]]; then
        VCPKG_INSTALLED_DIR="$SDK_VCPKG_INSTALLED_DIR"
    fi
fi

export SDK_BUILD SDK_PREFIX REPO_ROOT PRESET="$CHRONON3D_INSTALL_TEST_PRESET"
export VCPKG_INSTALLED_DIR="${VCPKG_INSTALLED_DIR:-$REPO_ROOT/vcpkg_installed/$PRESET}"
export VCPKG_TARGET_TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-linux}"

# Phase 2: destructive feature-OFF ghost sweep, opt-in.
if [[ "${CHRONON3D_INSTALL_TEST_GHOST_SWEEP:-0}" == "1" ]]; then
    log "Phase 2: feature-OFF ghost sweep"
    bash "$SCRIPT_DIR/sdk/check_feature_ghosts.sh"
else
    log "Phase 2: skipped"
fi

log "Phase 3: archive canary gate"
bash "$SCRIPT_DIR/sdk/check_archive_canaries.sh"

log "Phase 4: existing external consumers"
bash "$SCRIPT_DIR/sdk/run_external_consumer.sh"

log "Phase 5: installed authoring asset consumer"
bash "$SCRIPT_DIR/sdk/run_asset_authoring_consumer.sh"

printf '{"test":"install_consumer_ci","status":"passed","preset":"%s","fast":%s,"ghost_sweep":%s,"authoring_assets":true}\n' \
    "$PRESET" \
    "${CHRONON3D_INSTALL_TEST_FAST:-0}" \
    "${CHRONON3D_INSTALL_TEST_GHOST_SWEEP:-0}"
