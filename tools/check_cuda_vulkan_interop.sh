#!/usr/bin/env bash
set -euo pipefail

# Fail-closed capability probe for the native CUDA -> Vulkan bridge.
# FFmpeg's hwmap is only a diagnostic: this build cannot derive a Vulkan
# device from CUDA frames even though the native external-memory path works.

INPUT="${1:-}"
if [[ -z "${INPUT}" || ! -f "${INPUT}" ]]; then
    echo "usage: $0 /path/to/input.mp4" >&2
    exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

command -v ffmpeg >/dev/null || { echo "ffmpeg not found" >&2; exit 2; }
command -v vulkaninfo >/dev/null || { echo "vulkaninfo not found" >&2; exit 2; }

HWACCELS="$(ffmpeg -hide_banner -hwaccels 2>&1 || true)"
if ! grep -qx 'cuda' <<<"${HWACCELS}"; then
    echo "INTEROP_UNAVAILABLE: FFmpeg CUDA hwaccel is missing" >&2
    exit 3
fi
if ! vulkaninfo --summary 2>/dev/null | grep -q 'VK_KHR_external_memory_capabilities'; then
    echo "INTEROP_UNAVAILABLE: Vulkan external-memory capability is missing" >&2
    exit 3
fi

PROBE="${CHRONON_CUDA_VULKAN_PROBE:-${ROOT_DIR}/tools/run_cuda_vulkan_external_memory_probe.sh}"
if [[ "${PROBE}" == *.sh ]]; then
    if [[ -n "${CUDA_INCLUDE:-}" ]]; then
        CUDA_INCLUDE="${CUDA_INCLUDE}" CUDA_LIB_DIR="${CUDA_LIB_DIR:-/usr/local/cuda/lib64}" \
            "${PROBE}"
    else
        "${PROBE}"
    fi
else
    "${PROBE}"
fi

echo "INTEROP_PASS: native CUDA/Vulkan external memory and semaphore bridge verified"
