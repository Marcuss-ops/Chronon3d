#!/usr/bin/env bash
# Phase 5 allocation gate. The benchmark harness writes one line named
# `steady_state_allocations_per_frame`; this gate refuses to infer a value from
# framebuffer allocations or from an absent report.
set -euo pipefail

GATE_NAME=steady_state_allocations
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
REPORT="${CHRONON_STEADY_STATE_REPORT:-$REPO_ROOT/docs/baselines/steady-state-allocations-certified.txt}"
THRESHOLD="${CHRONON_STEADY_STATE_THRESHOLD:-0}"

if [[ ! -s "$REPORT" ]]; then
    echo "GATE_FAIL_INTERNAL: missing allocation report: $REPORT" >&2
    echo "  run the P1E/Phase-5 harness and set CHRONON_STEADY_STATE_REPORT" >&2
    exit 2
fi

value="$(awk -F= '/^steady_state_allocations_per_frame=/ { gsub(/[[:space:]]/, "", $2); print $2; exit }' "$REPORT")"
if [[ -z "$value" ]]; then
    echo "GATE_FAIL_INTERNAL: report has no steady_state_allocations_per_frame=..." >&2
    exit 2
fi

if ! awk -v value="$value" -v threshold="$THRESHOLD" 'BEGIN { exit !(value >= 0 && value <= threshold) }'; then
    echo "GATE_FAIL: $GATE_NAME value=$value threshold=$THRESHOLD report=$REPORT" >&2
    exit 1
fi

echo "GATE_PASS: $GATE_NAME steady_state_allocations_per_frame=$value threshold=$THRESHOLD report=$REPORT"
