#!/usr/bin/env bash
# scripts/ci/verify_vulkan_gated_unit_tests.sh
#
# Vulkan-gated unit lane.
#
# ── Why this exists ────────────────────────────────────────────────────────
# The Vulkan-gated suites (tests/shader_abi_tests.cmake) only compile when
# CHRONON3D_ENABLE_VULKAN=ON. No workflow did that: native-video.yml configures
# the SAME `linux-video-test` preset with -DCHRONON3D_ENABLE_VULKAN=OFF, so a
# merge that breaks the Vulkan backend, or that leaves a test asserting a
# contract which was deliberately changed, could land with every lane green.
# That is exactly how the 2026-09-13 merge produced a non-compiling HEAD
# (new authority methods swallowed into a `private:` section) and two stale
# assertions (see TICKET-AUDIT-2026-09-12-REMEDIATION.md, "quarta sessione").
#
# ── Scope ─────────────────────────────────────────────────────────────────
# These are UNIT/INTEGRATION targets that run headless (they do not require a
# GPU device to be present): the surface authority uses a fake materializer,
# and the shader ABI check only reads the embedded SPIR-V.
#
# `chronon3d_text_tile_raster_canary_tests` is EXCLUDED on purpose — but NOT
# because it is red. It is GREEN (78/78, 2026-09-16, evidence
# docs/evidence/text-tile-canary-2026-09-16.md); the recorded 65/78 came from a
# binary built hours before the ABI fix `5b59cfd48`, in a target no gate ever
# rebuilt. It stays out of THIS lane because it needs a real Vulkan device
# (pixel readback), while this lane runs headless on ubuntu-latest → adding it
# here would yield a silent skip, not a signal. Its gate is the GPU/WBH one:
# `tools/verify_text_tile_gpu_canary.sh` (registered in
# tools/gates/manifest.sh WBH_ONLY_GATES).
#
# Usage:
#   bash scripts/ci/verify_vulkan_gated_unit_tests.sh [build-dir]
#
# Environment:
#   JOBS                     parallel build jobs (default: nproc)
#   CHRONON3D_VULKAN_LANE_PRESET  configure preset (default: linux-video-test)
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
PRESET="${CHRONON3D_VULKAN_LANE_PRESET:-linux-video-test}"
BUILD_DIR="${1:-${ROOT}/build/chronon/${PRESET}}"

TARGETS=(
    chronon3d_shader_abi_consistency_tests
    chronon3d_vulkan_debug_context_tests
    chronon3d_vulkan_descriptor_arena_tests
    chronon3d_vulkan_kernel_store_tests
    chronon3d_vulkan_surface_materializer_tests
    chronon3d_vulkan_interop_generation_tests
    chronon3d_vulkan_submission_ring_tests
    chronon3d_vulkan_upload_ring_tests
)

# Reuse an already-configured tree when one is present (fast local loop); a
# clean CI checkout configures the preset here.
if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "verify_vulkan_gated_unit_tests: configuring preset ${PRESET}"
    cmake --preset "${PRESET}" \
        -DCHRONON3D_ENABLE_VULKAN=ON \
        -DCHRONON3D_ENABLE_CUDA_INTEROP=OFF
fi

if ! grep -q "CHRONON3D_ENABLE_VULKAN:BOOL=ON" "${BUILD_DIR}/CMakeCache.txt"; then
    echo "verify_vulkan_gated_unit_tests: ${BUILD_DIR} was configured without Vulkan" >&2
    exit 1
fi

echo "verify_vulkan_gated_unit_tests: building ${#TARGETS[@]} Vulkan-gated targets"
cmake --build "${BUILD_DIR}" --target "${TARGETS[@]}" -j "${JOBS:-$(nproc)}"

# ctest -R takes a regex; the target names are the test names (doctest suites
# registered 1:1 in tests/shader_abi_tests.cmake).
FILTER="$(IFS='|'; echo "${TARGETS[*]}")"
echo "verify_vulkan_gated_unit_tests: running ctest -R '${FILTER}'"
ctest --test-dir "${BUILD_DIR}" -R "${FILTER}" --output-on-failure --timeout 120
