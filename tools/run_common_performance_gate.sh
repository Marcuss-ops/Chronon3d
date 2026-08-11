#!/usr/bin/env bash
# Common performance certification gate for every benchmark block.
# Usage: bash tools/run_common_performance_gate.sh --report <bench.v3.json>
# Missing observability returns GATE_BLOCKED (exit 2), not a synthetic PASS.
set -uo pipefail

GATE_NAME="run_common_performance_gate"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "${1:-}" != "--report" || -z "${2:-}" || "$#" -ne 2 ]]; then
    echo "[ERROR] ${GATE_NAME}: usage: $0 --report <bench.v3.json>" >&2
    echo "GATE_BLOCKED"
    exit 2
fi

exec python3 "${SCRIPT_DIR}/check_common_performance_gate.py" --report "$2"
