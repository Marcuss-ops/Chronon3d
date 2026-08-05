#!/usr/bin/env bash
# Verify frame 0-59 independent/shared graph-cache parity in all required orders.
set -euo pipefail

GATE_NAME=verify_sequential_graph_cache_linux
ROOT="$(git rev-parse --show-toplevel)"
BUILD_DIR="${CHRONON3D_BUILD_DIR:-$ROOT/build/chronon/linux-fast-dev}"
TARGET="chronon3d_deterministic_tests"
BINARY="$BUILD_DIR/tests/$TARGET"
SOURCE="$ROOT/tests/deterministic/test_sequential_graph_cache.cpp"
SUITE_CMAKE="$ROOT/tests/deterministic_tests.cmake"
SCENE_TARGET="chronon3d_scene_tests"
SCENE_BINARY="$BUILD_DIR/tests/$SCENE_TARGET"
SCENE_SOURCE="$ROOT/tests/render_graph/pipeline/test_graph_cache.cpp"

RUNTIME_SOURCES=(
    "$ROOT/src/render_graph/pipeline/graph_cache_coordinator.cpp"
    "$ROOT/src/render_graph/pipeline/scene_refresh.cpp"
    "$ROOT/src/render_graph/pipeline/refresh/source.cpp"
    "$ROOT/src/render_graph/builder/graph_builder_layer_pipeline.cpp"
    "$ROOT/src/render_graph/nodes/source_node.cpp"
    "$ROOT/src/render_graph/executor/cache_evaluator.cpp"
)

if [ ! -d "$BUILD_DIR" ]; then
    echo "GATE_FAIL: build directory not found: $BUILD_DIR" >&2
    exit 2
fi

# Incremental builds only: this gate never removes the build tree and does
# not rebuild unrelated targets.
cmake --build "$BUILD_DIR" --target "$TARGET" "$SCENE_TARGET" -j"${CHRONON3D_BUILD_JOBS:-2}"

check_fresh_binary() {
    local binary="$1"
    shift
    [ -x "$binary" ] || {
        echo "GATE_FAIL: test binary not found or not executable: $binary" >&2
        exit 2
    }
    for source in "$@"; do
        [ ! "$source" -nt "$binary" ] || {
            echo "GATE_FAIL: stale test binary; $source is newer than $binary" >&2
            exit 2
        }
    done
}

check_fresh_binary "$BINARY" "$SOURCE" "$SUITE_CMAKE" "${RUNTIME_SOURCES[@]}"
check_fresh_binary "$SCENE_BINARY" "$SCENE_SOURCE" "${RUNTIME_SOURCES[@]}"

echo "== sequential graph-cache parity (cold/warm, opacity=0.0, anti-black, all orders) =="
"$BINARY" --test-case='Sequential graph cache verifier:*' --no-skip

echo "== transactional refresh and topology mismatch contract =="
"$SCENE_BINARY" --test-case='GraphCache - *' --no-skip

echo "CHRONON_SEQUENTIAL_GRAPH_CACHE_PASS"
echo "[INFO] ${GATE_NAME}: cold/warm cache, opacity 0.0, anti-black, no partial refresh, and linear/random/reverse/repeated orders passed"
