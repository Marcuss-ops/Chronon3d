#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"

if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
    echo "Text health: '${build_dir}' is not a configured CMake build directory." >&2
    echo "Configure it with tests and text enabled, then rerun:" >&2
    echo "  cmake -S . -B ${build_dir} -DCHRONON3D_BUILD_TESTS=ON -DCHRONON3D_ENABLE_TEXT=ON -DCHRONON3D_USE_BLEND2D=ON" >&2
    exit 2
fi

cmake --build "${build_dir}" --target chronon3d_text_health_tests
ctest --test-dir "${build_dir}" \
    --output-on-failure \
    -R '^chronon3d_text_health_tests$'
