#!/usr/bin/env bash
# Verify that diagnostics translation units disappear from the SDK archive when
# the canonical CHRONON3D_ENABLE_DIAGNOSTICS feature is disabled.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=./common.sh
source "$HERE/common.sh"

: "${SDK_PREFIX:?SDK_PREFIX env var required}"
: "${SDK_BUILD:?SDK_BUILD env var required}"
: "${REPO_ROOT:?REPO_ROOT env var required}"
: "${PRESET:?PRESET env var required}"

log "ghost sweep starting (tests=OFF diagnostics=OFF)"

# BUILD_TESTS forces diagnostics ON in the root policy, so disable tests before
# asserting the feature-OFF archive. This keeps the ghost check honest.
cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DCHRONON3D_BUILD_TESTS=OFF \
    -DCHRONON3D_ENABLE_DIAGNOSTICS=OFF 1>&2 \
    || fail "ghost sweep: diagnostics-OFF reconfigure failed"

cmake --build "$SDK_BUILD" --target chronon3d_sdk_impl -j8 1>&2 \
    || fail "ghost sweep: chronon3d_sdk_impl rebuild failed"
cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" 1>&2 \
    || fail "ghost sweep: cmake --install failed"

impl_archive="$(find "$SDK_PREFIX" -type f -name 'libchronon3d_sdk_impl.a' 2>/dev/null | head -1 || true)"
[[ -n "$impl_archive" ]] || fail "libchronon3d_sdk_impl.a missing after diagnostics-OFF install"

GATE_TMP="$(mktemp_dir chronon3d_install_gate_off)"
cleanup_register "$GATE_TMP"
ar_list="$GATE_TMP/ar_off.txt"
ar t "$impl_archive" > "$ar_list" || fail "ar t failed on diagnostics-OFF archive"

# Use the real source TU basenames emitted by the diagnostics OBJECT target.
for ghost in bbox_overlay.cpp.o layout_preview_overlay.cpp.o nulls_overlay.cpp.o; do
    if grep -qF -- "$ghost" "$ar_list"; then
        fail "GHOST-FAIL: $ghost leaked into diagnostics-OFF archive"
    fi
done

log "GHOST-OK: diagnostics-OFF archive contains no diagnostics translation units"
