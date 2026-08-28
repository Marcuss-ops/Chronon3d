#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MANIFEST="$ROOT/tools/wbh_toolchain_manifest.env"
BUILD_DIR=${WBH_BUILD_DIR:-"$ROOT/.tmp/wbh-smoke"}
SMOKE_BIN="$BUILD_DIR/cuda_smoke"
PROBE_BIN=${WBH_NVENC_PROBE_BIN:-"$ROOT/.tmp/chronon-builds/linux-fast-dev/tools/chronon3d_cuda_vulkan_nvenc_probe"}

# shellcheck disable=SC1090
source "$MANIFEST"

fail() { printf 'WBH_FAIL: %s\n' "$*" >&2; exit 1; }
pass() { printf 'WBH_PASS: %s\n' "$*"; }
version_ge() {
    [[ "$(printf '%s\n' "$1" "$2" | sort -V | head -n1)" == "$2" ]]
}

[[ "$(uname -s)" == "$WBH_OS_FAMILY" ]] || fail "OS is $(uname -s), expected $WBH_OS_FAMILY"
command -v cmake >/dev/null || fail "cmake missing"
cmake_version=$(cmake --version | awk 'NR==1 {print $3}')
version_ge "$cmake_version" "$WBH_CMAKE_MIN_VERSION" || fail "cmake $cmake_version < $WBH_CMAKE_MIN_VERSION"
command -v ninja >/dev/null || fail "ninja missing"
ninja_version=$(ninja --version | sed 's/[^0-9.].*//')
version_ge "$ninja_version" "$WBH_NINJA_MIN_VERSION" || fail "ninja $ninja_version < $WBH_NINJA_MIN_VERSION (1.13 is not accepted)"
pass "cmake=$cmake_version ninja=$ninja_version"

command -v nvcc >/dev/null || fail "nvcc missing: install the pinned CUDA Toolkit on the WBH"
nvcc --version | grep -q 'release' || fail "nvcc --version did not report a CUDA release"
pass "nvcc available"

command -v vulkaninfo >/dev/null || fail "vulkaninfo missing"
vulkaninfo --summary >/dev/null 2>&1 || fail "vulkaninfo failed"
pass "Vulkan instance available"

command -v nvidia-smi >/dev/null || fail "nvidia-smi missing"
gpu_line=$(nvidia-smi --query-gpu=name,driver_version,uuid --format=csv,noheader | head -n1)
[[ -n "$gpu_line" ]] || fail "NVIDIA GPU not detected"
pass "NVIDIA GPU: $gpu_line"

mkdir -p "$BUILD_DIR"
nvcc "$ROOT/tools/cuda_smoke.cu" -O2 -o "$SMOKE_BIN"
"$SMOKE_BIN"
pass "CUDA kernel smoke"

if [[ -x "$PROBE_BIN" ]]; then
    probe_output=$(
        "$PROBE_BIN" 2>&1
    ) || fail "Vulkan/NVENC probe failed"
    grep -q 'CUDA_VULKAN_NVENC_PASS' <<<"$probe_output" || fail "probe did not emit CUDA_VULKAN_NVENC_PASS"
    pass "Vulkan/CUDA/NVENC probe"
else
    printf 'WBH_DEFERRED: NVENC probe executable not found at %s\n' "$PROBE_BIN"
fi

pass "toolchain preflight complete"
