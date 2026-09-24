#!/usr/bin/env bash
# Validate a chronon3d.stats.v1 post-warmup node-memory report.
set -euo pipefail

if [[ $# -ne 1 || ! -f "$1" ]]; then
    echo "GATE_FAIL: usage: $0 <node-memory-report.json>" >&2
    exit 2
fi
if ! command -v jq >/dev/null 2>&1; then
    echo "GATE_FAIL: jq is required to validate node-memory reports" >&2
    exit 2
fi

report=$1
if ! jq -e '
    def nonnegative_integer: type == "number" and . >= 0 and floor == .;
    type == "object" and
    .schema == "chronon3d.stats.v1" and
    (.session_id | type == "string" and length > 0) and
    (.scene_id | type == "string" and length > 0) and
    (.frame_count | nonnegative_integer and . > 0) and
    (.nodes | type == "array" and length > 0) and
    (([.nodes[].node_id] | unique | length) == (.nodes | length)) and
    all(.nodes[];
        type == "object" and
        (.node_id | type == "string" and length > 0) and
        (.node_type | type == "string" and length > 0) and
        (.["pixels_read"] | nonnegative_integer) and
        (.["pixels_written"] | nonnegative_integer) and
        (.["bytes_read"] | nonnegative_integer) and
        (.["bytes_written"] | nonnegative_integer) and
        (.["framebuffer_copies"] | nonnegative_integer) and
        (.["framebuffer_clears"] | nonnegative_integer) and
        (.["allocations"] | nonnegative_integer) and
        (.["allocated_bytes"] | nonnegative_integer) and
        (.["temporary_buffers"] | nonnegative_integer) and
        (.["live_bytes"] | nonnegative_integer) and
        (.["peak_live_bytes"] | nonnegative_integer)
    )
' "$report" >/dev/null; then
    echo "GATE_FAIL: report is missing required schema or node measurements: $report" >&2
    exit 1
fi

hot_node_count=$(jq '[.nodes[] | select(.node_type == "Composite" or .node_type == "Output" or .node_type == "TextRun" or .node_type == "Effect" or .node_type == "Transform")] | length' "$report")
if (( hot_node_count == 0 )); then
    echo "GATE_FAIL: report contains no Composite, Clear(Output), TextRun, Effect, or Transform measurements" >&2
    exit 1
fi

allocating_nodes=$(jq -r '[.nodes[] | select(.node_type == "Composite" or .node_type == "Output" or .node_type == "TextRun" or .node_type == "Effect" or .node_type == "Transform") | select(.allocations > 0) | .node_id] | join(", ")' "$report")
if [[ -n "$allocating_nodes" ]]; then
    echo "[INFO] check_node_memory_metrics: steady-state framebuffer allocations observed for hot nodes ${allocating_nodes}" >&2
    echo "GATE_FAIL: expected zero allocations in the post-warmup node-memory report" >&2
    exit 1
fi

node_count=$(jq '.nodes | length' "$report")
frame_count=$(jq '.frame_count' "$report")
echo "GATE_PASS: node-memory metrics present; ${node_count} nodes across ${frame_count} measured frames"
echo "[INFO] check_node_memory_metrics: no steady-state framebuffer allocations observed for hot nodes"
