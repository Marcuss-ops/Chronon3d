#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
BUILD_DIR="${1:-${CHRONON_STYLIZE_GOLDEN_BUILD_DIR:-${ROOT}/build/chronon/linux-fast-dev}}"
TEST_BIN="${BUILD_DIR}/tests/chronon3d_effects_tests"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/chronon-stylize.XXXXXX")"
trap 'rm -rf "${TMP}"' EXIT

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "verify_stylize_goldens: missing configured build ${BUILD_DIR}" >&2
    exit 1
fi
cmake --build "${BUILD_DIR}" --target chronon3d_effects_tests -j "${JOBS:-10}"

for name in threshold mosaic; do
    upper="${name^^}"
    env "CHRONON_${upper}_RAW=${TMP}/${name}.rgba" \
        "${TEST_BIN}" --test-case="${upper^}FramehashCanary" --no-skip
    args=(--raw "${TMP}/${name}.rgba" --width 32 --height 32 --engine-sha unknown --plan-digest "${name}-canary-v1")
    if [[ "${UPDATE_GOLDEN:-0}" == "1" ]]; then
        python3 "${ROOT}/tools/raw_frame_hash.py" "${args[@]}" --out "${ROOT}/tests/effects/goldens/${name}.framehash"
    else
        python3 "${ROOT}/tools/raw_frame_hash.py" "${args[@]}" --golden "${ROOT}/tests/effects/goldens/${name}.framehash"
    fi
done
