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

if [ ! -d "$BUILD_DIR" ]; then
    echo "GATE_FAIL: build directory not found: $BUILD_DIR" >&2
    exit 2
fi

cmake --build "$BUILD_DIR" --target "$TARGET" -j"${CHRONON3D_BUILD_JOBS:-2}"

[ -x "$BINARY" ] || {
    echo "GATE_FAIL: test binary not found or not executable: $BINARY" >&2
    exit 2
}
[ ! "$SOURCE" -nt "$BINARY" ] || {
    echo "GATE_FAIL: stale test binary; $SOURCE is newer than $BINARY" >&2
    exit 2
}
[ ! "$SUITE_CMAKE" -nt "$BINARY" ] || {
    echo "GATE_FAIL: stale test binary; $SUITE_CMAKE is newer than $BINARY" >&2
    exit 2
}

"$BINARY" --test-case='Sequential graph cache verifier:*' --no-skip

echo "CHRONON_SEQUENTIAL_GRAPH_CACHE_PASS"
echo "[INFO] ${GATE_NAME}: frames 0-59 verified in linear, random, reverse, and repeated orders"
