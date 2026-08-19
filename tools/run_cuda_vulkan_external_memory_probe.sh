#!/usr/bin/env bash
set -euo pipefail

# Build/run the external-memory probe without installing the CUDA Toolkit.
# Set CUDA_INCLUDE when headers are supplied by a container or SDK mount.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CUDA_INCLUDE="${CUDA_INCLUDE:-/usr/local/cuda/include}"
CUDA_LIB_DIR="${CUDA_LIB_DIR:-/usr/local/cuda/lib64}"
OUT="${TMPDIR:-/tmp}/chronon-cuda-vulkan-probe"

if [[ ! -f "${CUDA_INCLUDE}/cuda.h" ]]; then
    echo "CUDA headers not found: ${CUDA_INCLUDE}/cuda.h" >&2
    echo "Set CUDA_INCLUDE to the CUDA Toolkit include directory." >&2
    exit 2
fi

g++ -std=c++20 -O2 -DCHRONON3D_ENABLE_CUDA_INTEROP \
    -I"${CUDA_INCLUDE}" \
    -I"${ROOT_DIR}/include" \
    -I"${ROOT_DIR}/vcpkg_installed/linux-fast-dev/x64-linux/include" \
    "${ROOT_DIR}/tools/cuda_vulkan_external_memory_probe.cpp" \
    "${ROOT_DIR}/src/backends/vulkan/cuda_vulkan_surface_bridge.cpp" \
    -o "${OUT}" -lvulkan -L"${CUDA_LIB_DIR}" -lcuda

exec "${OUT}"
