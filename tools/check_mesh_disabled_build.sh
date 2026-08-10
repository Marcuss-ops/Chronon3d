#!/usr/bin/env bash
# Verify that the core project compiles and its targeted preparation tests run
# with Mesh support disabled. This gate intentionally keeps public Mesh
# contracts available while proving the importer/backend dependency is absent.
set -euo pipefail

GATE_NAME=check_mesh_disabled_build
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${CHRONON3D_MESH_OFF_BUILD_DIR:-${ROOT_DIR}/build/mesh-off-gate}"
if [[ "${CHRONON3D_MESH_OFF_CLEAN:-1}" == "1" ]]; then
    rm -rf "$BUILD_DIR"
fi
TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE:-${ROOT_DIR}/cmake/Chronon3DVcpkgToolchain.cmake}"
VCPKG_INSTALLED_DIR="${CHRONON3D_VCPKG_INSTALLED_DIR:-${ROOT_DIR}/build/manual-test/vcpkg_installed}"
VCPKG_TARGET_TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-linux}"
if [[ ! -d "$VCPKG_INSTALLED_DIR/$VCPKG_TARGET_TRIPLET" ]]; then
    echo "GATE_FAIL: $GATE_NAME: vcpkg prefix not found: $VCPKG_INSTALLED_DIR/$VCPKG_TARGET_TRIPLET" >&2
    echo "Set CHRONON3D_VCPKG_INSTALLED_DIR to a populated vcpkg installed prefix." >&2
    exit 2
fi
JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-8}"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DVCPKG_INSTALLED_DIR="$VCPKG_INSTALLED_DIR" \
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET" \
    -DCMAKE_PREFIX_PATH="$VCPKG_INSTALLED_DIR/$VCPKG_TARGET_TRIPLET" \
    -DCMAKE_MODULE_PATH="$VCPKG_INSTALLED_DIR/$VCPKG_TARGET_TRIPLET/share/stb;$ROOT_DIR/cmake" \
    -DVCPKG_MANIFEST_MODE=ON \
    -DVCPKG_MANIFEST_FEATURES='tests;text;blend2d' \
    -DCHRONON3D_ENABLE_MESH=OFF \
    -DCHRONON3D_BUILD_TESTS=ON \
    -DCHRONON3D_BUILD_CLI=OFF \
    -DCHRONON3D_BUILD_CONTENT=OFF \
    -DCHRONON3D_BUILD_DIAGNOSTICS=OFF \
    -DCHRONON3D_USE_BLEND2D=ON \
    -DCHRONON3D_ENABLE_TEXT=ON \
    -DCHRONON3D_ENABLE_VIDEO=OFF \
    -DCHRONON3D_ENABLE_EXR=OFF \
    -DCHRONON3D_ENABLE_NATIVE_FFMPEG=OFF \
    -DCHRONON3D_ENABLE_TELEMETRY=OFF \
    -DCHRONON3D_UNITY_BUILD=OFF

CACHE="$BUILD_DIR/CMakeCache.txt"
[[ -f "$CACHE" ]] || { echo "GATE_FAIL: $GATE_NAME: CMakeCache.txt missing" >&2; exit 1; }
grep -Eq '^CHRONON3D_ENABLE_MESH:BOOL=OFF$' "$CACHE" || {
    echo "GATE_FAIL: $GATE_NAME: CHRONON3D_ENABLE_MESH is not OFF" >&2
    exit 1
}

if [[ -f "$BUILD_DIR/build.ninja" ]] && grep -Eq 'meshoptimizer|mesh_renderer\.cpp|software_mesh_processor\.cpp|test_mesh_render\.cpp' "$BUILD_DIR/build.ninja"; then
    echo "GATE_FAIL: $GATE_NAME: Mesh implementation/dependency leaked into OFF build" >&2
    grep -nE 'meshoptimizer|mesh_renderer\.cpp|software_mesh_processor\.cpp|test_mesh_render\.cpp' "$BUILD_DIR/build.ninja" >&2 || true
    exit 1
fi

# Build only the OFF-specific smoke target. The broad architecture aggregate also
# builds unrelated content/text test executables, which are intentionally outside
# this dependency-disabled gate when CHRONON3D_BUILD_CONTENT=OFF.
cmake --build "$BUILD_DIR" --parallel "$JOBS" --target chronon3d_mesh_disabled_smoke

TEST_BIN="$BUILD_DIR/tests/chronon3d_mesh_disabled_smoke"
TEST_SRC="$ROOT_DIR/tests/assets/mesh_disabled_smoke.cpp"
[[ -x "$TEST_BIN" ]] || { echo "GATE_FAIL: $GATE_NAME: core test binary missing" >&2; exit 1; }
[[ ! "$TEST_SRC" -nt "$TEST_BIN" ]] || {
    echo "GATE_FAIL: $GATE_NAME: stale core test binary" >&2
    exit 1
}

"$TEST_BIN" --test-case='*disabled-feature*' --no-skip --success
"$TEST_BIN" --test-case='*cache keys*' --no-skip --success

echo "GATE_PASS: $GATE_NAME: CHRONON3D_ENABLE_MESH=OFF core build and targeted diagnostics PASS"
echo "[INFO] $GATE_NAME: meshoptimizer, Mesh renderer/processor, and Mesh golden sources absent"
