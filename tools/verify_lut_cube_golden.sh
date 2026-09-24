#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
BUILD_DIR="${1:-${CHRONON_LUT_GOLDEN_BUILD_DIR:-${ROOT}/build/chronon/linux-fast-dev}}"
TEST_BIN="${BUILD_DIR}/tests/chronon3d_effects_tests"
GOLDEN="${ROOT}/tests/effects/goldens/lut_cube.framehash"
RAW="$(mktemp "${TMPDIR:-/tmp}/chronon-lut-cube.XXXXXX.raw")"
trap 'rm -f "${RAW}"' EXIT

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "verify_lut_cube_golden: missing configured build ${BUILD_DIR}" >&2
    exit 1
fi

cmake --build "${BUILD_DIR}" --target chronon3d_effects_tests -j "${JOBS:-10}"
CHRONON_LUT_CUBE_RAW="${RAW}" "${TEST_BIN}" --test-case='LutCubeFramehashCanary' --no-skip
if [[ "${UPDATE_GOLDEN:-0}" == "1" ]]; then
    python3 "${ROOT}/tools/raw_frame_hash.py" --raw "${RAW}" --width 32 --height 32 \
        --engine-sha unknown --plan-digest lut-cube-canary-v1 --out "${GOLDEN}"
else
    python3 "${ROOT}/tools/raw_frame_hash.py" --raw "${RAW}" --width 32 --height 32 \
        --engine-sha unknown --plan-digest lut-cube-canary-v1 --golden "${GOLDEN}"
fi
