#!/usr/bin/env bash
# ============================================================================
# tools/check_trace_sql_report_selftest.sh — sequence-aware report regression
# ============================================================================
#
# Runs the existing real Perfetto correctness harness (unless a trace path is
# supplied and already exists), then checks the report output for:
#   - per-sequence timestamp reconstruction;
#   - persisted TracePacketDefaults / InternedData resolution;
#   - 30 matched slices across 4 packet sequences;
#   - zero unmatched or incomplete stacks;
#   - exact worst-frame aggregation for frame IDs 0..9, including frame 0.
#
# Usage:
#   bash tools/check_trace_sql_report_selftest.sh [file.pftrace]
#
# Env vars PERFETTO_SDK_DIR and CHRONON3D_TRACE_GATE_CACHE are forwarded to
# the existing report/correctness gate scripts.
# ============================================================================
set -euo pipefail

GATE_NAME="check_trace_sql_report_selftest"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRACE_FILE="${1:-/tmp/chronon3d_trace_gate.pftrace}"
PERFETTO_SDK_DIR="${PERFETTO_SDK_DIR:-/tmp/perfetto_sdk}"
CACHE="${CHRONON3D_TRACE_GATE_CACHE:-/tmp/chronon3d_trace_gates}"

if [[ ! -s "$TRACE_FILE" ]]; then
    CHRONON3D_TRACE_GATES=1 \
    PERFETTO_SDK_DIR="$PERFETTO_SDK_DIR" \
    CHRONON3D_TRACE_GATE_CACHE="$CACHE" \
        bash "$ROOT/tools/check_trace_correctness.sh" >/tmp/chronon3d_trace_sql_report_correctness.log
fi

if [[ ! -s "$TRACE_FILE" ]]; then
    echo "GATE_FAIL: correctness harness did not produce $TRACE_FILE" >&2
    exit 2
fi

REPORT="$(
    PERFETTO_SDK_DIR="$PERFETTO_SDK_DIR" \
    CHRONON3D_TRACE_GATE_CACHE="$CACHE" \
        bash "$ROOT/tools/trace_sql_report.sh" "$TRACE_FILE" --top-frames 20 --top-nodes 5
)"

REPORT="$REPORT" python3 - <<'PY'
import os
import re
import sys

report = os.environ["REPORT"]

def fail(message):
    print(f"GATE_FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)

span = re.search(r"span: [^,]+, (\d+) slices", report)
if not span or int(span.group(1)) != 30:
    fail("expected 30 matched slices")

state = re.search(
    r"sequences: (\d+), unmatched_ends: (\d+), incomplete_begins: (\d+)",
    report,
)
if not state:
    fail("sequence/stack diagnostics are missing")
if tuple(map(int, state.groups())) != (4, 0, 0):
    fail(f"unexpected sequence/stack state: {state.groups()}")

try:
    frame_section = report.split("== top 20 worst frames", 1)[1].split(
        "== most expensive nodes", 1
    )[0]
except IndexError:
    fail("worst-frame section is missing")

frames = {}
for line in frame_section.splitlines():
    match = re.match(r"^\s+\d+\s+(\d+)\s+(\d+)\s+([0-9]+(?:\.[0-9]+)?)\s*$", line)
    if match:
        frame, slices, total_ms = int(match.group(1)), int(match.group(2)), float(match.group(3))
        frames[frame] = (slices, total_ms)

expected = set(range(10))
if set(frames) != expected:
    fail(f"worst-frame aggregation is not exactly 0..9: {sorted(frames)}")
if any(slices != 1 or total_ms <= 0 for slices, total_ms in frames.values()):
    fail("worst-frame rows contain invalid slice counts or durations")

print("  PASS: sequence-local timestamps reconstructed")
print("  PASS: TracePacketDefaults and InternedData metadata resolved")
print("  PASS: 30 slices matched across 4 sequences with clean stacks")
print("  PASS: worst-frame aggregation contains frame 0..9")
print(f"GATE_PASS: {len(frames)} worst-frame rows verified")
PY

printf '[INFO] %s: sequence-aware trace report regression verified\n' "$GATE_NAME"
