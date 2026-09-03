#!/usr/bin/env bash
# run_wbh_gates.sh — canonical Working Build Host gate chain.
#
# Gate membership lives only in tools/gates/manifest.sh::WBH_ONLY_GATES.
# This runner executes that list sequentially and fail-fast; adding or removing
# a WBH gate requires changing the manifest, not this file.
#
# Usage:
#   bash tools/run_wbh_gates.sh
#   CHRONON3D_GATE_PROFILE=wbh bash tools/wrap_push.sh origin main

set -euo pipefail

GATE_NAME="run_wbh_gates"
REPO_ROOT="$(git rev-parse --show-toplevel)"
SCRIPT_DIR="${REPO_ROOT}/tools"

# shellcheck source=gates/manifest.sh
source "${SCRIPT_DIR}/gates/manifest.sh"

GATE_COUNT=${#WBH_ONLY_GATES[@]}
echo "${GATE_NAME}: starting WBH gate chain (${GATE_COUNT} gates, sourced from gates/manifest.sh)..."

idx=0
for gate in "${WBH_ONLY_GATES[@]}"; do
    idx=$((idx + 1))
    if [ ! -x "${SCRIPT_DIR}/${gate}" ]; then
        if [ ! -f "${SCRIPT_DIR}/${gate}" ]; then
            echo "GATE_FAIL_INTERNAL: ${gate} missing (path: ${SCRIPT_DIR}/${gate})" >&2
            exit 2
        fi
        echo "GATE_FAIL_INTERNAL: ${gate} not executable (path: ${SCRIPT_DIR}/${gate})" >&2
        echo "  fix: chmod +x tools/${gate}" >&2
        exit 2
    fi
    echo "  [${idx}/${GATE_COUNT}] ${gate}"
    if ! bash "${SCRIPT_DIR}/${gate}"; then
        rc=$?
        echo "GATE_FAIL: ${gate} (exit $rc)" >&2
        exit 1
    fi
done

echo "GATE_PASS: ${GATE_COUNT}/${GATE_COUNT} WBH gates PASSED"
echo "[INFO] ${GATE_NAME}: ${GATE_COUNT}/${GATE_COUNT} WBH verification gates executed successfully in sequence (sourced from gates/manifest.sh)"
exit 0
