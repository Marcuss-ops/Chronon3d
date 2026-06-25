#!/usr/bin/env bash
# tools/install_consumer_test.sh
# ─────────────────────────────────────────────────────────────────────
# End-to-end install boundary CI test for Chronon3D SDK.
#
# Pipeline:
#   1. Configure + build the SDK to a temp dir.
#   2. `cmake --install` into an isolated temp prefix.
#   3. Verify boundary artifacts (config, targets, archive, headers).
#   4. Configure + build + run the standalone consumer project at
#      `tests/install_consumer/` against the temp prefix.
#   5. Validate that the consumer stdout contains [BOUNDARY-OK]
#      and that it produced a non-empty PNG output.
#   6. Emit a single-line JSON summary for CI log scraping.
#
# Wired into CTest via `add_test(NAME install_consumer_ci COMMAND …)`
# in the top-level CMakeLists.txt, gated by the cache var
# CHRONON3D_BUILD_INSTALL_CONSUMER_TEST (enabled by the linux-ci preset).
#
# Exit codes:
#   0 = boundary validated
#   1 = boundary broken
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

# ────────────────────────── Configuration ────────────────────────────
PRESET="${CHRONON3D_INSTALL_TEST_PRESET:-linux-ci}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_ROOT="${TMPDIR:-/tmp}"

log() { printf "[install_consumer_test] %s\n" "$*" >&2; }

# ────────────────────────── Tooling guard ─────────────────────────────
command -v cmake >/dev/null || { log "FAIL: cmake not on PATH"; exit 1; }
cmake_version="$(cmake --version | head -1 | awk '{print $3}')"
cmake_major="$(echo "$cmake_version" | cut -d. -f1)"
cmake_minor="$(echo "$cmake_version" | cut -d. -f2)"
if (( cmake_major < 3 || (cmake_major == 3 && cmake_minor < 25) )); then
    log "FAIL: cmake >=3.25 required (have $cmake_version)"
    exit 1
fi
log "cmake $cmake_version at $(command -v cmake)"
log "repo root  : $REPO_ROOT"
log "preset     : $PRESET"

# ────────────────────────── Temp isolation ────────────────────────────
SDK_BUILD=""
SDK_PREFIX=""
CONS_BUILD=""
cleanup() {
    local rc=$?
    [[ -n "$SDK_BUILD" ]] && rm -rf "$SDK_BUILD"
    [[ -n "$SDK_PREFIX" ]] && rm -rf "$SDK_PREFIX"
    [[ -n "$CONS_BUILD" ]] && rm -rf "$CONS_BUILD"
    exit "$rc"
}
trap cleanup EXIT

SDK_BUILD="$(mktemp -d "$TMP_ROOT/chronon3d_install_consumer_sdk_build.XXXXXX")"
SDK_PREFIX="$(mktemp -d "$TMP_ROOT/chronon3d_install_consumer_prefix.XXXXXX")"
CONS_BUILD="$(mktemp -d "$TMP_ROOT/chronon3d_install_consumer_build.XXXXXX")"
log "temp sdk build : $SDK_BUILD"
log "temp install   : $SDK_PREFIX"
log "temp consumer  : $CONS_BUILD"

# ────────────────────────── Step 1: build SDK ─────────────────────────
log "Configuring SDK at $SDK_BUILD (preset=$PRESET)"
cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" 1>&2

log "Building SDK target 'chronon3d_sdk_impl'"
cmake --build "$SDK_BUILD" --target chronon3d_sdk_impl 1>&2

# ────────────────────────── Step 2: install ───────────────────────────
log "Installing SDK into $SDK_PREFIX"
cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" 1>&2

# ────────────────────────── Step 3: boundary pre-conditions ───────────
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

# ────────────────────────── Step 4: consumer ──────────────────────────
log "Configuring consumer (tests/install_consumer/)"
CONS_PREFIX_PATH="$SDK_PREFIX"
if [[ -n "${VCPKG_INSTALLED_DIR:-}" ]]; then
    local_vcpkg_triplet="${VCPKG_TARGET_TRIPLET:-x64-linux}"
    CONS_PREFIX_PATH="$CONS_PREFIX_PATH;${VCPKG_INSTALLED_DIR}/${local_vcpkg_triplet}"
fi
if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    CONS_PREFIX_PATH="$CONS_PREFIX_PATH;${CMAKE_PREFIX_PATH}"
fi

CMAKE_ARGS=(
    "-S" "$REPO_ROOT/tests/install_consumer"
    "-B" "$CONS_BUILD"
    "-DCMAKE_PREFIX_PATH=$CONS_PREFIX_PATH"
    "-DCMAKE_BUILD_TYPE=Release"
)
VCPKG_TC="$REPO_ROOT/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake"
if [[ -f "$VCPKG_TC" ]]; then
    CMAKE_ARGS+=( "-DCMAKE_TOOLCHAIN_FILE=$VCPKG_TC" )
elif [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
    CMAKE_ARGS+=( "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}" )
fi
cmake "${CMAKE_ARGS[@]}" 1>&2

log "Building consumer"
cmake --build "$CONS_BUILD" --target check_install 1>&2

consumer_bin="$CONS_BUILD/check_install"
if [[ ! -x "$consumer_bin" ]]; then
    log "FAIL: consumer binary not found"
    exit 1
fi

log "Running consumer"
consumer_output="$("$consumer_bin")"
if [[ "$consumer_output" != *"[BOUNDARY-OK]"* ]]; then
    log "FAIL: consumer missing [BOUNDARY-OK] marker"
    printf "%s\n" "$consumer_output" >&2
    exit 1
fi
log "Consumer: $consumer_output"

# ────────────────────────── Step 5: verify PNG output ─────────────────
output_png="$CONS_BUILD/sdk_consumer_output.png"
if [[ ! -f "$output_png" ]]; then
    log "FAIL: output PNG not found: $output_png"
    exit 1
fi
png_size="$(stat -c%s "$output_png" 2>/dev/null || echo 0)"
if (( png_size == 0 )); then
    log "FAIL: output PNG is empty"
    exit 1
fi
log "PNG verified: $output_png ($png_size bytes)"

# ────────────────────────── Step 6: summary ───────────────────────────
printf '{"test":"install_consumer_ci","status":"passed","preset":"%s","png_bytes":%d}\n' \
    "$PRESET" "$png_size"
