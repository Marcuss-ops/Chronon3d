#!/usr/bin/env bash
# tools/sdk/check_archive_members.sh
# ─────────────────────────────────────────────────────────────────────
# Phase 1 of tools/install_consumer_test.sh:
#   1. Configure SDK at $SDK_BUILD (preset=$PRESET).
#   2. Build the SDK target chronon3d_sdk_impl.
#   3. cmake --install into $SDK_PREFIX.
#   4. Verify boundary artifacts (config, targets, archive, headers)
#      are present at the install prefix.
#
# Reads from env: PRESET, REPO_ROOT, SDK_BUILD, SDK_PREFIX.
# Emits for downstream phases: IMPL_ARCHIVE=<path-to-installed-archive>.
#
# Exit codes:
#   0 = boundary pre-conditions satisfied (downstream phases may run)
#   1 = configure / build / install / boundary failed
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

require_cmake_3_25
make_temp_dirs

log "repo root  : $REPO_ROOT"
log "preset     : $PRESET"
log "temp sdk build : $SDK_BUILD"
log "temp prefix    : $SDK_PREFIX"

# ── Step 1: build SDK ──────────────────────────────────────────────
log "Configuring SDK at $SDK_BUILD (preset=$PRESET)"
cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" 1>&2

log "Building SDK target 'chronon3d_sdk_impl'"
cmake --build "$SDK_BUILD" --target chronon3d_sdk_impl 1>&2

# ── Step 2: install ────────────────────────────────────────────────
log "Installing SDK into $SDK_PREFIX"
cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" 1>&2

# ── Step 3: boundary pre-conditions ────────────────────────────────
config_file="$(find "$SDK_PREFIX" -type f -name 'Chronon3DConfig.cmake' 2>/dev/null | head -1 || true)"
targets_file="$(find "$SDK_PREFIX" -type f -name 'Chronon3DTargets.cmake' 2>/dev/null | head -1 || true)"
impl_archive="$(find "$SDK_PREFIX" -type f -name 'libchronon3d_sdk_impl.a' 2>/dev/null | head -1 || true)"
headers_dir="$SDK_PREFIX/include/chronon3d"

if [[ -z "$config_file" ]]; then
    log "FAIL: Chronon3DConfig.cmake not installed"
    exit 1
fi
if [[ -z "$targets_file" ]]; then
    log "FAIL: Chronon3DTargets.cmake not installed"
    exit 1
fi
if [[ -z "$impl_archive" ]]; then
    log "FAIL: libchronon3d_sdk_impl.a not installed"
    exit 1
fi
if [[ ! -d "$headers_dir" ]]; then
    log "FAIL: public headers not installed"
    exit 1
fi
header_count="$(find "$headers_dir" -maxdepth 6 -type f -name '*.hpp' 2>/dev/null | wc -l | tr -d ' ')"
if (( header_count == 0 )); then
    log "FAIL: no .hpp under $headers_dir"
    exit 1
fi
log "Boundary verified: config, targets, archive, $header_count headers"

# Downstream phases (canary gate, consumer runner) consume the archive
# path via env; exporting keeps common.sh untouched.
export IMPL_ARCHIVE="$impl_archive"
log "IMPL_ARCHIVE=$IMPL_ARCHIVE"
