#!/usr/bin/env bash
set -euo pipefail

# Fail-closed capability probe for the required CUDA -> Vulkan frame bridge.
# This deliberately tests an actual CUDA frame mapping, not just the presence
# of Vulkan/CUDA libraries or filter names.

INPUT="${1:-}"
if [[ -z "${INPUT}" || ! -f "${INPUT}" ]]; then
    echo "usage: $0 /path/to/input.mp4" >&2
    exit 2
fi

command -v ffmpeg >/dev/null || { echo "ffmpeg not found" >&2; exit 2; }
command -v vulkaninfo >/dev/null || { echo "vulkaninfo not found" >&2; exit 2; }

HWACCELS="$(ffmpeg -hide_banner -hwaccels 2>&1 || true)"
if ! grep -qx 'cuda' <<<"${HWACCELS}"; then
    echo "INTEROP_UNAVAILABLE: FFmpeg CUDA hwaccel is missing" >&2
    exit 3
fi
FILTER_LIST="$(ffmpeg -hide_banner -filters 2>&1 || true)"
if ! grep -q 'hwmap' <<<"${FILTER_LIST}"; then
    echo "INTEROP_UNAVAILABLE: FFmpeg hwmap filter is missing" >&2
    exit 3
fi
if ! vulkaninfo --summary 2>/dev/null | grep -q 'VK_KHR_external_memory_capabilities'; then
    echo "INTEROP_UNAVAILABLE: Vulkan external-memory capability is missing" >&2
    exit 3
fi

LOG_FILE="$(mktemp)"
trap 'rm -f "${LOG_FILE}"' EXIT
if ! ffmpeg -hide_banner -loglevel error -y \
    -hwaccel cuda -hwaccel_output_format cuda -i "${INPUT}" \
    -vf 'hwmap=derive_device=vulkan' -an -t 0.1 \
    -c:v h264_nvenc -pix_fmt cuda -f null - >"${LOG_FILE}" 2>&1; then
    echo "INTEROP_UNAVAILABLE: CUDA frame cannot be imported into Vulkan" >&2
    sed -n '1,12p' "${LOG_FILE}" >&2
    exit 4
fi

echo "INTEROP_PASS: CUDA frame imported into Vulkan without host download"
