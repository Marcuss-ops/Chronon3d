#!/usr/bin/env bash
# Build and run the regression suites that guard render-graph/backend changes.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${CHRONON3D_GATE_BUILD_DIR:-${ROOT}/build/chronon/linux-video-test}"
JOBS="${CHRONON3D_GATE_JOBS:-$(nproc 2>/dev/null || printf '14')}"

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "registry/render_graph/cache gate: missing configured build directory: ${BUILD_DIR}" >&2
    exit 1
fi

echo "registry/render_graph/cache gate: building focused targets in ${BUILD_DIR} with -j${JOBS}"
# One ninja invocation for all three targets: a per-target loop serialises
# three dependency graphs that share most of their object files, paying a
# full ninja start-up and a separate compile wave for overlapping sources.
cmake --build "${BUILD_DIR}" \
    --target chronon3d_backend_registry_tests \
    --target chronon3d_render_graph_tests \
    --target chronon3d_cache_tests \
    -j"${JOBS}"

echo "registry/render_graph/cache gate: running focused regression suites"
ctest --test-dir "${BUILD_DIR}" \
    -R '^(chronon3d_backend_registry_tests|chronon3d_render_graph_tests|chronon3d_cache_tests)$' \
    -j"${JOBS}" \
    --output-on-failure
