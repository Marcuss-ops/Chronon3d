#!/usr/bin/env bash
set -euo pipefail

# ──────────────────────────────────────────────────────────────────
#  build-fast.sh —  Sviluppo fulmineo per Chronon3d
#  Usage:
#    ./build-fast.sh                    # build preset linux-fast-dev
#    ./build-fast.sh turbo              # ultra-fast clean build (Debug, no tests)
#    ./build-fast.sh turbo-inc dev      # rebuild only changed CLI group + relink
#    ./build-fast.sh turbo-inc render   # rebuild only changed CLI group + relink
#    ./build-fast.sh turbo-inc video    # rebuild only changed CLI group + relink
#    ./build-fast.sh arch               # build graph + core contracts
#    ./build-fast.sh executor           # build graph after executor changes
#    ./build-fast.sh scene              # build solo chronon3d_scene
#    ./build-fast.sh test "Extension*"  # build + run test pattern
#    ./build-fast.sh ext                # build solo chronon3d_extension
#──────────────────────────────────────────────────────────────────

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build/chronon/linux-fast-dev"
PRESET="linux-fast-dev"
TURBO_BUILD_DIR="$ROOT_DIR/build/chronon/linux-turbo"
TURBO_PRESET="linux-turbo"
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
    turbo)
        # ./build-fast.sh turbo  —  absolute fastest build (Debug, no tests, no content, unity batch 32)
        if [[ ! -f "$TURBO_BUILD_DIR/build.ninja" ]]; then
            cmake --preset "$TURBO_PRESET"
        fi
        echo "╔══════════════════════════════════════════╗"
        echo "║  🚀 TURBO build: chronon3d_cli only      ║"
        echo "╚══════════════════════════════════════════╝"
        cmake --build "$TURBO_BUILD_DIR" --target chronon3d_cli -j "$JOBS"
        echo "✅ Turbo build done."
        ;;
    turbo-inc)
        # ./build-fast.sh turbo-inc <group>
        # Rebuild only the changed CLI group lib + relink the CLI executable.
        # Groups: dev, render, video, telemetry, bench, core
        GROUP="${2:-}"
        if [[ -z "$GROUP" ]]; then
            echo "Usage: ./build-fast.sh turbo-inc <group>"
            echo "Groups: dev | render | video | telemetry | bench | core"
            echo ""
            echo "  dev        — batch, studio, inspect, camera_path"
            echo "  render     — render, preflight, proofs, bake_layer, graph"
            echo "  video      — video export, ffmpeg pipe, chunked"
            echo "  telemetry  — telemetry summary/report"
            echo "  bench      — benchmark commands"
            echo "  core       — list, info, watch, doctor"
            exit 1
        fi
        # Validate turbo build dir exists (must have done a full turbo build first)
        if [[ ! -f "$TURBO_BUILD_DIR/build.ninja" ]]; then
            echo "⚠️  Turbo build dir not found. Running full turbo build first..."
            cmake --preset "$TURBO_PRESET"
            cmake --build "$TURBO_BUILD_DIR" --target chronon3d_cli -j "$JOBS"
        fi
        # Map group name → CMake static library target
        case "$GROUP" in
            dev)        GROUP_TARGET="chronon3d_cli_dev" ;;
            render)     GROUP_TARGET="chronon3d_cli_render" ;;
            video)      GROUP_TARGET="chronon3d_cli_video" ;;
            telemetry)  GROUP_TARGET="chronon3d_cli_telemetry" ;;
            bench)      GROUP_TARGET="chronon3d_cli_bench" ;;
            core)       GROUP_TARGET="chronon3d_cli_core" ;;
            *)
                echo "❌ Unknown group: $GROUP"
                echo "Valid: dev | render | video | telemetry | bench | core"
                exit 1
                ;;
        esac
        echo "╔══════════════════════════════════════════╗"
        echo "║  ⚡ TURBO-INC: $GROUP → $GROUP_TARGET"
        echo "╚══════════════════════════════════════════╝"
        T0=$(date +%s%N)
        # Step 1: Rebuild only the changed group library
        cmake --build "$TURBO_BUILD_DIR" --target "$GROUP_TARGET" -j "$JOBS"
        T1=$(date +%s%N)
        # Step 2: Relink CLI executable (fast — just linker step)
        cmake --build "$TURBO_BUILD_DIR" --target chronon3d_cli -j "$JOBS"
        T2=$(date +%s%N)
        GROUP_MS=$(( (T1 - T0) / 1000000 ))
        TOTAL_MS=$(( (T2 - T0) / 1000000 ))
        echo "✅ Turbo-inc done. Group: ${GROUP_MS}ms | Total (inc relink): ${TOTAL_MS}ms"
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
