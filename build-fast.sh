#!/usr/bin/env bash
set -euo pipefail

# ──────────────────────────────────────────────────────────────────
#  build-fast.sh —  Sviluppo fulmineo per Chronon3d
#  Usage:
#    ./build-fast.sh                    # build preset linux-fast-dev
#    ./build-fast.sh scene              # build solo chronon3d_scene
#    ./build-fast.sh test "Extension*"  # build + run test pattern
#    ./build-fast.sh ext                # build solo chronon3d_extension
#──────────────────────────────────────────────────────────────────

BUILD_DIR="/home/pierone/Pyt/Chronon3d/build/chronon/linux-fast-dev"
PRESET="linux-fast-dev"
TARGET="${1:-}"

build_target() {
    local target="$1"
    echo "╔══════════════════════════════════════════╗"
    echo "║  Building: $target"
    echo "╚══════════════════════════════════════════╝"
    cmake --build "$BUILD_DIR" --target "$target" -j8 2>&1 | tail -10
}

case "${TARGET}" in
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
        local filter="${1:-*}"
        build_target "chronon3d_core_tests"
        echo ""
        echo "╔══════════════════════════════════════════╗"
        echo "║  Running tests: $filter"
        echo "╚══════════════════════════════════════════╝"
        $BUILD_DIR/tests/chronon3d_core_tests -tc="$filter"
        ;;
    all|fast|"")
        echo "╔══════════════════════════════════════════╗"
        echo "║  Building: cli + tests_fast (parallel)   ║"
        echo "╚══════════════════════════════════════════╝"
        cmake --build "$BUILD_DIR" --target chronon3d_cli -j8 &
        cmake --build "$BUILD_DIR" --target chronon3d_tests_fast -j8 &
        wait -n  # wait for first to finish
        wait     # wait for remaining
        echo "✅ Both targets built."
        ;;
    *)
        # Assume it's a CMake target name
        build_target "$TARGET"
        ;;
esac
