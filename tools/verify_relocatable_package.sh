#!/usr/bin/env bash
# ═════════════════════════════════════════════════════════════════════════════
# tools/verify_relocatable_package.sh
#
# Proves the installed SDK package is relocatable and self-contained enough to
# consume WITHOUT the Chronon source tree:
#
#   tar xf chronon3d-sdk.tar.gz
#   cmake -S external_project -B build -DCMAKE_PREFIX_PATH=<extracted>
#   cmake --build build
#   ./build/example
#
# Two independent gates:
#
#   Gate A  "no-repo / no-CMake / no-vcpkg" C ABI path — extract the tarball
#           into a scratch dir and compile a C consumer with a bare gcc
#           command (-I + -L + -lchronon3d_c), then run it.  This is the
#           self-contained binary-package contract Go/Rust/Python rely on.
#
#   Gate B  "find_package relocation" path — configure an external CMake
#           project against the EXTRACTED prefix (not the original install
#           prefix) so Chronon3DConfig.cmake must resolve its own location via
#           PACKAGE_PREFIX_DIR.  Links Chronon3D::C and runs it.
#
# Ends with exactly one marker on success:
#
#   CHRONON_SDK_RELOCATABLE_PASS
#
# Environment:
#   CHRONON3D_RELOCATE_FAST=1    reuse an existing SDK_BUILD + SDK_PREFIX
#   SDK_BUILD / SDK_PREFIX       required in FAST mode
#   CHRONON3D_RELOCATE_KEEP=1    keep temp dirs for debugging
# ═════════════════════════════════════════════════════════════════════════════
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${REPO_ROOT:-$(cd "$HERE/.." && pwd)}"
PRESET="${CHRONON3D_RELOCATE_PRESET:-linux-ci}"
FAST="${CHRONON3D_RELOCATE_FAST:-0}"
KEEP="${CHRONON3D_RELOCATE_KEEP:-0}"

log() { printf '[verify_relocatable] %s\n' "$*" >&2; }
fail() { log "FAIL: $*"; exit 1; }
mktemp_dir() { mktemp -d "${TMPDIR:-/tmp}/${1:-chronon3d_relocate}.XXXXXX"; }

WORK_DIR="$(mktemp_dir chronon3d_relocate)"
trap 'rc=$?; if [[ "$KEEP" != "1" ]]; then rm -rf "$WORK_DIR"; fi; exit "$rc"' EXIT

# ── 1. Materialize an installed prefix ──────────────────────────────────────
if [[ "$FAST" == "1" ]]; then
    : "${SDK_BUILD:?FAST mode requires SDK_BUILD}"
    : "${SDK_PREFIX:?FAST mode requires SDK_PREFIX}"
    [[ -d "$SDK_PREFIX" ]] || fail "SDK_PREFIX missing: $SDK_PREFIX"
else
    SDK_BUILD="$(mktemp_dir chronon3d_relocate_build)"
    SDK_PREFIX="$(mktemp_dir chronon3d_relocate_prefix)"
    cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
        -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" >&2 || fail "SDK configure failed"
    cmake --build "$SDK_BUILD" --target chronon3d_sdk_impl chronon3d_c >&2 \
        || fail "SDK build failed"
    cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" >&2 || fail "SDK install failed"
fi

[[ -f "$SDK_PREFIX/lib/cmake/Chronon3D/Chronon3DConfig.cmake" ]] \
    || fail "installed config missing"
[[ -e "$SDK_PREFIX/lib/libchronon3d_c.so" ]] \
    || fail "installed libchronon3d_c.so missing"

# ── 2. Package + extract into a scratch "fresh machine" ─────────────────────
TARBALL="$WORK_DIR/chronon3d-sdk.tar"
EXTRACTED="$WORK_DIR/extracted/opt/chronon"
mkdir -p "$(dirname "$EXTRACTED")"

log "packaging $SDK_PREFIX -> chronon3d-sdk.tar"
tar cf "$TARBALL" -C "$SDK_PREFIX" . || fail "tar create failed"

log "extracting tarball into $EXTRACTED"
mkdir -p "$EXTRACTED"
tar xf "$TARBALL" -C "$EXTRACTED" || fail "tar extract failed"

# Sanity: the extracted tree must be usable on its own (no source tree).
[[ -f "$EXTRACTED/include/chronon3d/c_api/chronon3d.h" ]] \
    || fail "extracted C ABI header missing"
[[ -e "$EXTRACTED/lib/libchronon3d_c.so" ]] \
    || fail "extracted libchronon3d_c.so missing"

# ── External consumer sources (created fresh, NOT from the repo) ────────────
EXTERNAL="$WORK_DIR/external_project"
mkdir -p "$EXTERNAL"
cat > "$EXTERNAL/main.c" <<'CEOF'
#include <chronon3d/c_api/chronon3d.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    static const char plan[] =
        "{\"schema\":\"chronon.render-plan\",\"version\":1,"
        "\"canvas\":{\"width\":4,\"height\":4,\"fps\":30,\"duration_frames\":1},"
        "\"layers\":[{\"id\":\"bg\",\"type\":\"color\","
        "\"color\":[0.2,0.4,0.6,1.0]}],"
        "\"output\":{\"path\":\"out.png\"}}";

    chronon_engine_config cfg = {sizeof(cfg), chronon_abi_version(), 0, 0};
    chronon_engine* engine = 0;
    chronon_error_info err = {sizeof(err), CHRONON_OK, 0};
    if (chronon_engine_create_v2(&cfg, &engine, &err) != CHRONON_OK || !engine) {
        fprintf(stderr, "create failed\n");
        return 1;
    }
    chronon_plan* p = 0;
    if (chronon_plan_compile_json_n(engine, plan, (uint64_t)(sizeof(plan) - 1), &p)
            != CHRONON_OK || !p) {
        fprintf(stderr, "compile failed\n");
        chronon_engine_destroy(engine);
        return 1;
    }
    chronon_frame_info info = {0, 0, 0, 0, 0};
    if (chronon_render_frame_into(engine, p, 0, 0, 0, &info)
            != CHRONON_ERROR_BUFFER_TOO_SMALL || info.size == 0) {
        fprintf(stderr, "size query failed\n");
        chronon_plan_destroy(p);
        chronon_engine_destroy(engine);
        return 1;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)info.size);
    if (!buf) { chronon_plan_destroy(p); chronon_engine_destroy(engine); return 1; }
    int status = chronon_render_frame_into(engine, p, 0, buf, info.size, &info);
    int nonzero = 0;
    for (uint64_t i = 0; i < info.size; ++i) nonzero |= buf[i] != 0;
    free(buf);
    chronon_plan_destroy(p);
    chronon_engine_destroy(engine);
    if (status != CHRONON_OK || !nonzero) {
        fprintf(stderr, "render failed or empty (status=%d)\n", status);
        return 1;
    }
    puts("RELOC_C_PASS");
    return 0;
}
CEOF

# ── Gate A: bare-gcc C ABI (no CMake, no vcpkg, no repo) ────────────────────
log "Gate A: bare gcc against extracted package"
gcc "$EXTERNAL/main.c" \
    -I"$EXTRACTED/include" \
    -L"$EXTRACTED/lib" \
    -lchronon3d_c \
    -o "$WORK_DIR/example_a" || fail "Gate A compile failed"

set +e
OUT_A="$(LD_LIBRARY_PATH="$EXTRACTED/lib" "$WORK_DIR/example_a" 2>&1)"
RC_A=$?
set -e
[[ "$RC_A" -eq 0 && "$OUT_A" == *"RELOC_C_PASS"* ]] \
    || fail "Gate A run failed: $OUT_A"
log "Gate A: $OUT_A"

# ── Gate B: find_package relocation via CMake ───────────────────────────────
cat > "$EXTERNAL/CMakeLists.txt" <<'CMEOF'
cmake_minimum_required(VERSION 3.27)
project(reloc_consumer C)
find_package(Chronon3D CONFIG REQUIRED)
if(NOT TARGET Chronon3D::C)
    message(FATAL_ERROR "Chronon3D::C target absent after relocation")
endif()
add_executable(example main.c)
set_target_properties(example PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_link_libraries(example PRIVATE Chronon3D::C)
CMEOF

log "Gate B: cmake configure against EXTRACTED prefix"
CMAKE_B="$WORK_DIR/build_b"
prefix_path="$EXTRACTED"
[[ -n "${VCPKG_INSTALLED_DIR:-}" ]] \
    && prefix_path="${prefix_path};${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET:-x64-linux}"
cmake -S "$EXTERNAL" -B "$CMAKE_B" \
    "-DCMAKE_PREFIX_PATH=$prefix_path" \
    -DCMAKE_BUILD_TYPE=Release >&2 \
    || fail "Gate B configure failed (find_package from moved prefix)"

log "Gate B: build"
cmake --build "$CMAKE_B" >&2 || fail "Gate B build failed"

set +e
OUT_B="$(LD_LIBRARY_PATH="$EXTRACTED/lib" "$CMAKE_B/example" 2>&1)"
RC_B=$?
set -e
[[ "$RC_B" -eq 0 && "$OUT_B" == *"RELOC_C_PASS"* ]] \
    || fail "Gate B run failed: $OUT_B"
log "Gate B: $OUT_B"

echo "CHRONON_SDK_RELOCATABLE_PASS"
exit 0
