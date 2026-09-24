#!/usr/bin/env bash
# Render generated-effect canary frames through the software effect registry,
# quantize with Chronon's linear-to-sRGB channel conversion, and compare hashes.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
BUILD_DIR="${1:-${CHRONON_TIER2_GOLDEN_BUILD_DIR:-${ROOT}/build/chronon/linux-fast-dev}}"
TARGET=chronon3d_effects_tests
TEST_BIN="${BUILD_DIR}/tests/${TARGET}"
GOLDEN="${ROOT}/tests/effects/goldens/tier2_generators.framehash"
RAW="$(mktemp "${TMPDIR:-/tmp}/chronon-tier2-generators.XXXXXX.raw")"
trap 'rm -f "${RAW}"' EXIT

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "verify_tier2_generators_golden: missing configured build ${BUILD_DIR}" >&2
    exit 1
fi

cmake --build "${BUILD_DIR}" --target "${TARGET}" -j "${JOBS:-10}"
CHRONON_TIER2_GENERATORS_RAW="${RAW}" \
    "${TEST_BIN}" --test-case='Tier2GeneratorFramehashCanary' --no-skip

if [[ "${UPDATE_GOLDEN:-0}" == "1" ]]; then
    python3 "${ROOT}/tools/raw_frame_hash.py" --raw "${RAW}" \
        --width 32 --height 32 --engine-sha unknown \
        --plan-digest tier2-generators-canary-v1 --out "${GOLDEN}"
    echo "verify_tier2_generators_golden: updated ${GOLDEN}"
else
    python3 "${ROOT}/tools/raw_frame_hash.py" --raw "${RAW}" \
        --width 32 --height 32 --engine-sha unknown \
        --plan-digest tier2-generators-canary-v1 --golden "${GOLDEN}"
fi
