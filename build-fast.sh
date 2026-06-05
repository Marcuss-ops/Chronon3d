#!/usr/bin/env bash
set -euo pipefail

# ──────────────────────────────────────────────────────────────────
#  build-fast.sh —  Sviluppo fulmineo per Chronon3d
#  Usage:
#    ./build-fast.sh                    # build preset linux-fast-dev
#    ./build-fast.sh arch               # build graph + core contracts
#    ./build-fast.sh executor           # build graph after executor changes
#    ./build-fast.sh video              # build video CLI tests
#    ./build-fast.sh scene              # build solo chronon3d_scene
#    ./build-fast.sh test "Extension*"  # build + run test pattern
#    ./build-fast.sh ext                # build solo chronon3d_extension
#──────────────────────────────────────────────────────────────────

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build/chronon/linux-fast-dev"
PRESET="linux-fast-dev"
TARGET="${1:-}"
JOBS="${JOBS:-$(nproc)}"

if [[ -z "${VCPKG_ROOT:-}" && -f "/home/pierone/vcpkg/scripts/buildsystems/vcpkg.cmake" ]]; then
    export VCPKG_ROOT="/home/pierone/vcpkg"
fi

ensure_configured() {
    if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
        cmake --preset "$PRESET"
    fi
}

build_target() {
    local target="$1"
    ensure_configured
    echo "╔══════════════════════════════════════════╗"
    echo "║  Building: $target"
    echo "╚══════════════════════════════════════════╝"
    cmake --build "$BUILD_DIR" --target "$target" -j "$JOBS"
}

run_test_binary() {
    local binary="$1"
    local filter="${2:-*}"
    build_target "$binary"
    "$BUILD_DIR/tests/$binary" -tc="$filter"
}

case "${TARGET}" in
    arch)
        build_target "chronon3d_graph"
        run_test_binary "chronon3d_core_tests" "CoreContract*"
        ;;
    executor)
        build_target "chronon3d_graph"
        run_test_binary "chronon3d_core_tests" "*framebuffer*lifetime*"
        ;;
    video)
        build_target "chronon3d_cli_tests"
        "$BUILD_DIR/tests/chronon3d_cli_tests" -tc="*PipeExportSession*,*RenderLoop*"
        ;;
    scene)
        build_target "chronon3d_scene"
        ;;
    ext|extension)
        build_target "chronon3d_extension"
        ;;
    pipeline|pipe)
        build_target "chronon3d_pipeline"
        ;;
    cli)
        build_target "chronon3d_cli"
        ;;
    test)
        # ./build-fast.sh test "ExtensionLoader*"
        shift 1 2>/dev/null || true
        filter="${1:-*}"
        run_test_binary "chronon3d_core_tests" "$filter"
        ;;
    scene-test)
        shift 1 2>/dev/null || true
        filter="${1:-*}"
        run_test_binary "chronon3d_scene_tests" "$filter"
        ;;
    cli-test)
        shift 1 2>/dev/null || true
        filter="${1:-*}"
        run_test_binary "chronon3d_cli_tests" "$filter"
        ;;
    ctest)
        shift 1 2>/dev/null || true
        filter="${1:-chronon3d_(core|scene|cli)_tests}"
        ensure_configured
        echo ""
        echo "╔══════════════════════════════════════════╗"
        echo "║  Running tests: $filter"
        echo "╚══════════════════════════════════════════╝"
        ctest --test-dir "$BUILD_DIR" --output-on-failure -R "$filter"
        ;;
    all|fast|"")
        echo "╔══════════════════════════════════════════╗"
        echo "║  Building: cli + tests_fast (parallel)   ║"
        echo "╚══════════════════════════════════════════╝"
        ensure_configured
        cmake --build "$BUILD_DIR" --target chronon3d_cli -j "$JOBS" &
        cmake --build "$BUILD_DIR" --target chronon3d_tests_fast -j "$JOBS" &
        wait -n  # wait for first to finish
        wait     # wait for remaining
        echo "✅ Both targets built."
        ;;
    *)
        # Assume it's a CMake target name
        build_target "$TARGET"
        ;;
esac
