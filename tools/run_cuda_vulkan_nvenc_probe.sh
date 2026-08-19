#!/usr/bin/env bash
set -euo pipefail

# Runs the build-integrated Vulkan -> CUDA -> NVENC device-only proof.
# Configure/build with CHRONON3D_ENABLE_CUDA_INTEROP=ON and
# CHRONON3D_ENABLE_NATIVE_FFMPEG=ON first.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${CHRONON_BUILD_DIR:-${ROOT_DIR}/.tmp/chronon-builds/native-verify}"
PROBE="${BUILD_DIR}/src/backends/vulkan/chronon3d_cuda_vulkan_nvenc_probe"

if [[ ! -x "${PROBE}" ]]; then
    echo "probe not built: ${PROBE}" >&2
    echo "configure with CHRONON3D_ENABLE_CUDA_INTEROP=ON and CHRONON3D_ENABLE_NATIVE_FFMPEG=ON" >&2
    exit 2
fi

exec "${PROBE}"
