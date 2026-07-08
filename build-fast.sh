#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Build location: project-local .tmp for deterministic builds, avoid /tmp tmpfs bottleneck.
BUILD_DIR="${BUILD_DIR_OVERRIDE:-${ROOT_DIR}/.tmp/chronon-builds/linux-fast-dev}"

# ccache: persistent, project-local by default. 20 GiB cap and sloppiness
# tuned for Clang/GCC PCH header drift.
export CCACHE_DIR="${CCACHE_DIR:-${ROOT_DIR}/.ccache}"

show_help() {
    cat <<EOF
build-fast.sh — Fast development builds for Chronon3d

Usage:
  ./build-fast.sh [command] [args...]

Commands:
  (none)              Build chronon3d_dev_fast (CLI + fast tests)
  arch                Build graph + run core contract tests
  executor            Build graph + run framebuffer lifetime tests
  scene               Build chronon3d_scene only
  ext, extension      Build chronon3d_extension only
  pipeline, pipe      Build a concrete target instead (chronon3d_pipeline is INTERFACE)
  cli                 Build chronon3d_cli only
  test [pattern]      Build + run core tests matching pattern
  scene-test [pat]    Build + run scene tests matching pattern
  cli-test [pat]      Build + run CLI tests matching pattern
  ctest [filter]      Run ctest with filter (default: core|scene|cli)
  content             Build with linux-content-dev (CLI + content + video ON, no telemetry)
  dashboard           Build with linux-dashboard-dev (CLI + content + telemetry + diagnostics)
  turbo               Ultra-fast Debug build (CLI only, no tests/content)
  turbo-inc <group>   Incremental rebuild of CLI group + relink
    Groups: dev | render | video | telemetry | bench | core
  release             RelWithDebInfo build (optimised + debug symbols, 2-5× faster runtime)

Environment:
  JOBS                       Parallel jobs (default: nproc)
  CCACHE_DIR                 ccache store (default: \${HOME}/.ccache, max 20G)
  BUILD_DIR_OVERRIDE         Override the resolved build dir (default: /tmp/chronon-builds/linux-fast-dev)

Notes:
  - Build dir defaults to tmpfs (/tmp/chronon-builds/linux-fast-dev).
    Set BUILD_DIR_OVERRIDE for an on-disk location.
  - ccache config is auto-bootstrapped on first run (skipped when CCACHE_DIR
    is explicitly set to a non-default path to avoid clobbering CI caches).

Examples:
  ./build-fast.sh                          # single Ninja invocation for dev_fast
  ./build-fast.sh test "Extension*"        # run matching core tests
  ./build-fast.sh turbo-inc video          # incremental video group rebuild
  JOBS=8 ./build-fast.sh cli               # limit parallel jobs
EOF
    exit 0
}

TARGET="${1:-}"
if [[ "$TARGET" == "-h" || "$TARGET" == "--help" || "$TARGET" == "help" ]]; then
    show_help
fi

# ── --target flag: consume flag + next arg as CMake target ───────────
if [[ "$TARGET" == "--target" ]]; then
    TARGET="${2:-}"
    if [[ -z "$TARGET" ]]; then
        echo "Error: --target requires a CMake target name" >&2
        echo "Usage: ./build-fast.sh --target <cmake-target>" >&2
        exit 1
    fi
    shift 1 2>/dev/null || true
fi

bootstrap_ccache() {
    # Never touch CCACHE_DIRs explicitly set by the user or CI.
    if [[ "$CCACHE_DIR" != "${HOME}/.ccache" ]]; then
        [[ -d "$CCACHE_DIR" ]] || mkdir -p "$CCACHE_DIR"
        return 0
    fi
    [[ -d "$CCACHE_DIR" ]] || mkdir -p "$CCACHE_DIR"
    if [[ ! -f "$CCACHE_DIR/ccache.conf" ]]; then
        cat > "$CCACHE_DIR/ccache.conf" <<CCACHE_EOF
# Chronon3d ccache settings — auto-bootstrapped by build-fast.sh
max_size = 20G
sloppiness = include_file_mtime,include_file_ctime,time_macros,pch_defines,file_macro
compression = true
compression_level = 6
hash_dir = false
cache_dir_levels = 3
temporary_dir = ${CCACHE_DIR}/tmp
CCACHE_EOF
        mkdir -p "${CCACHE_DIR}/tmp"
        echo "→ ccache config bootstrapped at $CCACHE_DIR/ccache.conf"
    fi
}

resolve_build_dir() {
    # Honor explicit override first.
    if [[ -n "${BUILD_DIR_OVERRIDE:-}" ]]; then
        BUILD_DIR="$BUILD_DIR_OVERRIDE"
        return
    fi
    # Default: derive from PRESET name so 'PRESET=linux-content-dev' Just Works.
    BUILD_DIR="/tmp/chronon-builds/${PRESET}"
    mkdir -p "$BUILD_DIR"
    local symlink="$ROOT_DIR/build/chronon/${PRESET}"
    mkdir -p "$(dirname "$symlink")"
    ln -sfnT "$BUILD_DIR" "$symlink"
}

PRESET="linux-fast-dev"
bootstrap_ccache
resolve_build_dir
TURBO_BUILD_DIR="$ROOT_DIR/build/chronon/linux-turbo"
TURBO_PRESET="linux-turbo"
RELEASE_BUILD_DIR="${BUILD_DIR_OVERRIDE:-${ROOT_DIR}/.tmp/chronon-builds/linux-fast-dev-release}"
RELEASE_PRESET="linux-fast-dev-release"
JOBS="${JOBS:-8}"

ensure_configured() {
    if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
        cmake --preset "$PRESET" -B "$BUILD_DIR"
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
        # chronon3d_graph is an INTERFACE target — build the test binary directly
        run_test_binary "chronon3d_core_tests" "CoreContract*"
        ;;
    executor)
        # chronon3d_graph is an INTERFACE target — build the test binary directly
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
        echo "chronon3d_pipeline is an INTERFACE target — use 'cli' or build a concrete target instead"
        exit 1
        ;;
    graph)
        echo "chronon3d_graph is an INTERFACE target — use 'cli' or build a concrete target instead"
        exit 1
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
    content)
        # ./build-fast.sh content —  engine + content ON (no telemetry)
        CONTENT_BUILD_DIR="${BUILD_DIR_OVERRIDE:-${ROOT_DIR}/.tmp/chronon-builds/linux-content-dev}"
        CONTENT_PRESET="linux-content-dev"
        content_symlink="$ROOT_DIR/build/chronon/linux-content-dev"
        mkdir -p "$CONTENT_BUILD_DIR"
        mkdir -p "$(dirname "$content_symlink")"
        ln -sfnT "$CONTENT_BUILD_DIR" "$content_symlink"
        if [[ ! -f "$CONTENT_BUILD_DIR/build.ninja" ]]; then
            cmake --preset "$CONTENT_PRESET" -B "$CONTENT_BUILD_DIR"
        fi
        echo "╔══════════════════════════════════════════╗"
        echo "║  📦 CONTENT build: CLI + content ON      ║"
        echo "╚══════════════════════════════════════════╝"
        cmake --build "$CONTENT_BUILD_DIR" --target chronon3d_dev_fast -j "$JOBS"
        echo "✅ Content build done."
        ;;
    dashboard)
        # ./build-fast.sh dashboard —  engine + content + telemetry + diagnostics
        DASH_BUILD_DIR="${BUILD_DIR_OVERRIDE:-${ROOT_DIR}/.tmp/chronon-builds/linux-dashboard-dev}"
        DASH_PRESET="linux-dashboard-dev"
        dash_symlink="$ROOT_DIR/build/chronon/linux-dashboard-dev"
        mkdir -p "$DASH_BUILD_DIR"
        mkdir -p "$(dirname "$dash_symlink")"
        ln -sfnT "$DASH_BUILD_DIR" "$dash_symlink"
        if [[ ! -f "$DASH_BUILD_DIR/build.ninja" ]]; then
            cmake --preset "$DASH_PRESET" -B "$DASH_BUILD_DIR"
        fi
        echo "╔══════════════════════════════════════════╗"
        echo "║  📊 DASHBOARD build: content + telemetry ║"
        echo "╚══════════════════════════════════════════╝"
        cmake --build "$DASH_BUILD_DIR" --target chronon3d_dev_fast -j "$JOBS"
        echo "✅ Dashboard build done."
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
    release|r)
        # ./build-fast.sh release  —  RelWithDebInfo (optimised + debug)
        # Uses its own tmpfs build dir, symlinked to build/chronon/linux-fast-dev-release
        release_symlink="$ROOT_DIR/build/chronon/linux-fast-dev-release"
        mkdir -p "$RELEASE_BUILD_DIR"
        mkdir -p "$(dirname "$release_symlink")"
        ln -sfnT "$RELEASE_BUILD_DIR" "$release_symlink"
        if [[ ! -f "$RELEASE_BUILD_DIR/build.ninja" ]]; then
            cmake --preset "$RELEASE_PRESET" -B "$RELEASE_BUILD_DIR"
        fi
        echo "╔══════════════════════════════════════════╗"
        echo "║  ⚡ RELEASE build: RelWithDebInfo        ║"
        echo "║  chronon3d_dev_fast (-O2 -g)            ║"
        echo "╚══════════════════════════════════════════╝"
        cmake --build "$RELEASE_BUILD_DIR" --target chronon3d_dev_fast -j "$JOBS"
        echo "✅ Release build done."
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
        echo "║  Building: dev_fast (CLI + tests_fast)   ║"
        echo "╚══════════════════════════════════════════╝"
        ensure_configured
        # Single Ninja invocation — chronon3d_dev_fast aggregate target bundles
        # chronon3d_cli + chronon3d_tests_fast.
        cmake --build "$BUILD_DIR" --target chronon3d_dev_fast -j "$JOBS"
        echo "✅ Dev-fast build complete."
        ;;
    *)
        # Assume it's a CMake target name
        build_target "$TARGET"
        ;;
esac
