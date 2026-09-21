#!/usr/bin/env bash
# Build and run the regression suites that guard render-graph/backend changes.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${CHRONON3D_GATE_BUILD_DIR:-${ROOT}/build/chronon/linux-video-test}"
JOBS="${CHRONON3D_GATE_JOBS:-14}"

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "registry/render_graph/cache gate: missing configured build directory: ${BUILD_DIR}" >&2
    exit 1
fi

echo "registry/render_graph/cache gate: building focused targets in ${BUILD_DIR} with -j${JOBS}"
for target in \
    chronon3d_backend_registry_tests \
    chronon3d_render_graph_tests \
    chronon3d_cache_tests; do
    echo "registry/render_graph/cache gate: target=${target}"
    cmake --build "${BUILD_DIR}" --target "${target}" -j"${JOBS}"
done

echo "registry/render_graph/cache gate: running focused regression suites"
ctest --test-dir "${BUILD_DIR}" \
    -R '^(chronon3d_backend_registry_tests|chronon3d_render_graph_tests|chronon3d_cache_tests)$' \
    --output-on-failure
