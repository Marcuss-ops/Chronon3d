#!/usr/bin/env bash
# check_cli_tests.sh — pre-push contract gate for chronon3d_cli_tests.
#
# The CI workflow already runs this suite, but the canonical local developer
# chain must exercise the same CLI contract before a push. The build directory
# is caller-overridable so CI and GPU hosts can select their own preset.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET="${CHRONON3D_CLI_TEST_PRESET:-linux-fast-dev}"
BUILD_DIR="${CHRONON3D_CLI_TEST_BUILD_DIR:-${ROOT_DIR}/.tmp/chronon-builds/${PRESET}}"
JOBS="${JOBS:-$(nproc 2>/dev/null || printf '2')}"

if [[ ! -f "${BUILD_DIR}/build.ninja" ]]; then
    echo "check_cli_tests: configuring ${PRESET} at ${BUILD_DIR}"
    cmake --preset "${PRESET}" -B "${BUILD_DIR}"
fi

echo "check_cli_tests: building chronon3d_cli_tests (${JOBS} jobs)"
cmake --build "${BUILD_DIR}" --target chronon3d_cli_tests -j "${JOBS}"

echo "check_cli_tests: running chronon3d_cli_tests (-j${JOBS})"
ctest --test-dir "${BUILD_DIR}" -R '^chronon3d_cli_tests$' -j"${JOBS}" --output-on-failure
echo "check_cli_tests: PASS"
