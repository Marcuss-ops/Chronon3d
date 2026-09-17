#!/usr/bin/env bash
# tools/verify_text_tile_gpu_canary.sh
#
# Real-GPU gate for the text tile raster canary
# (tests/backends/test_text_tile_raster_canary.cpp).
#
# ── Why this is separate from verify_vulkan_gated_unit_tests.sh ────────────
# That lane runs `ubuntu-latest` headless: it has no GPU device, and every suite
# it contains is deliberately device-free (fake materializer / embedded SPIR-V
# inspection). This canary dispatches the shipped text_tile_bin /
# text_tile_raster SPIR-V against a real Vulkan device and reads the pixels
# back, so wiring it into the headless lane would produce a silent skip, not a
# signal. It therefore belongs to the WBH (workstation build host) profile.
#
# ── History ────────────────────────────────────────────────────────────────
# The suite was recorded as 65/78 (13 pixel assertions at 0) and excluded from
# every lane. That number came from a binary built at 07:00 UTC while the ABI
# fix landed at 16:33 UTC (`5b59cfd48`); the target was never rebuilt because no
# gate built it. Rebuilt from the same tree it is 78/78 — see
# docs/evidence/text-tile-canary-2026-09-16.md.
#
# Usage:
#   tools/verify_text_tile_gpu_canary.sh [build-dir]
#
# Environment:
#   JOBS                          parallel build jobs (default: nproc)
#   CHRONON3D_TEXT_CANARY_PRESET  configure preset (default: linux-video-test)
#   CHRONON3D_TEXT_CANARY_BUILD_DIR  override the build directory
#   CHRONON3D_REQUIRE_TEXT_CANARY_DEVICE=1  treat a missing GPU as FAIL instead
#                                            of SKIP (use on GPU-certified hosts)
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
PRESET="${CHRONON3D_TEXT_CANARY_PRESET:-linux-video-test}"
BUILD_DIR="${1:-${CHRONON3D_TEXT_CANARY_BUILD_DIR:-${ROOT}/build/chronon/${PRESET}}}"
TARGET=chronon3d_text_tile_raster_canary_tests
EXPECTED_ASSERTIONS="${CHRONON3D_TEXT_CANARY_EXPECTED_ASSERTIONS:-78}"

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "verify_text_tile_gpu_canary: configuring preset ${PRESET}"
    cmake --preset "${PRESET}" -DCHRONON3D_ENABLE_VULKAN=ON
fi

if ! grep -q "CHRONON3D_ENABLE_VULKAN:BOOL=ON" "${BUILD_DIR}/CMakeCache.txt"; then
    echo "verify_text_tile_gpu_canary: ${BUILD_DIR} was configured without Vulkan" >&2
    exit 1
fi

echo "verify_text_tile_gpu_canary: building ${TARGET}"
cmake --build "${BUILD_DIR}" --target "${TARGET}" -j "${JOBS:-$(nproc)}"

BIN="${BUILD_DIR}/tests/${TARGET}"
if [[ ! -x "${BIN}" ]]; then
    echo "verify_text_tile_gpu_canary: ${BIN} is not executable" >&2
    exit 1
fi

OUT="$(mktemp)"
trap 'rm -f "${OUT}"' EXIT
set +e
"${BIN}" 2>&1 | tee "${OUT}"
RC="${PIPESTATUS[0]}"
set -e

ASSERTIONS="$(sed -n 's/.*assertions: *\([0-9]*\) *| *\([0-9]*\) *passed *| *\([0-9]*\) *failed.*/\1 \2 \3/p' "${OUT}" | tail -1)"
read -r N_ASSERT N_PASSED N_FAILED <<<"${ASSERTIONS:-0 0 0}"

if [[ "${N_ASSERT}" -eq 0 ]]; then
    if [[ "${CHRONON3D_REQUIRE_TEXT_CANARY_DEVICE:-0}" == "1" ]]; then
        echo "verify_text_tile_gpu_canary: FAIL — no Vulkan device with storage-image support" >&2
        exit 1
    fi
    echo "verify_text_tile_gpu_canary: SKIP — no Vulkan device with storage-image support on this host"
    exit 0
fi

if [[ "${RC}" -ne 0 || "${N_FAILED}" -ne 0 ]]; then
    echo "verify_text_tile_gpu_canary: FAIL — ${N_PASSED}/${N_ASSERT} assertions passed, ${N_FAILED} failed" >&2
    exit 1
fi

if [[ "${N_ASSERT}" -ne "${EXPECTED_ASSERTIONS}" ]]; then
    echo "verify_text_tile_gpu_canary: FAIL — ${N_ASSERT} assertions executed, expected ${EXPECTED_ASSERTIONS}" >&2
    echo "  the canary contract changed; re-certify before widening the expected count" >&2
    exit 1
fi

echo "verify_text_tile_gpu_canary: PASS — ${N_PASSED}/${N_ASSERT} assertions"
