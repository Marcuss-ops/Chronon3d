#!/usr/bin/env bash
# ============================================================================
# tools/trace_sql_report.sh — plan §24 .pftrace report (PerfettoSQL-style)
# ============================================================================
#
# Builds (once, cached) and runs tools/trace_gates/trace_sql_report.cpp: a
# standalone .pftrace report that needs no trace_processor.  It re-reads the
# trace with the Perfetto pbzero decoders shipped in the SDK header and
# prints:
#   - trace span + slice count
#   - top-N worst frames (per-frame total slice time via the `frame`
#     annotation)   -- equivalent PerfettoSQL:
#        SELECT frame, COUNT(*) slices, SUM(dur)/1e6 total_ms FROM slice
#        WHERE name='RenderFrame' GROUP BY frame ORDER BY total_ms DESC LIMIT 20;
#   - most expensive node (node_execute aggregated by `stable_node_id`,
#     falling back to slice name when no annotation is present)
#
# Requires the Perfetto amalgamation (perfetto.h + perfetto.cc) via
# PERFETTO_SDK_DIR (default /tmp/perfetto_sdk).  Build artifacts are cached
# in CHRONON3D_TRACE_GATE_CACHE (default /tmp/chronon3d_trace_gates),
# shared with the trace gates of plan §23.
#
# Usage:
#   bash tools/trace_sql_report.sh <file.pftrace> [--top-frames N] [--top-nodes N]
#
# Exit codes: 0 = report written, 1 = tool error, 2 = missing SDK / build error
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ "$#" -lt 1 ]]; then
    echo "usage: bash tools/trace_sql_report.sh <file.pftrace> [--top-frames N] [--top-nodes N]" >&2
    exit 2
fi
TRACE_FILE="$1"
shift

PERFETTO_SDK_DIR="${PERFETTO_SDK_DIR:-/tmp/perfetto_sdk}"
CACHE="${CHRONON3D_TRACE_GATE_CACHE:-/tmp/chronon3d_trace_gates}"
BIN="$CACHE/trace_sql_report"

if [[ ! -f "$TRACE_FILE" ]]; then
    echo "trace_sql_report: no such file: $TRACE_FILE" >&2
    exit 2
fi
if [[ ! -f "$PERFETTO_SDK_DIR/perfetto.h" || ! -f "$PERFETTO_SDK_DIR/perfetto.cc" ]]; then
    echo "trace_sql_report: Perfetto amalgamation not found in $PERFETTO_SDK_DIR" >&2
    echo "  (set PERFETTO_SDK_DIR to a dir containing perfetto.h + perfetto.cc)" >&2
    exit 2
fi

mkdir -p "$CACHE"

if [[ ! -f "$CACHE/perfetto.o" ]]; then
    echo "trace_sql_report: compiling perfetto amalgamation (first run only)..."
    g++ -std=c++20 -O2 -c "$PERFETTO_SDK_DIR/perfetto.cc" -o "$CACHE/perfetto.o"
fi

if [[ ! -f "$BIN" || "$ROOT/tools/trace_gates/trace_sql_report.cpp" -nt "$BIN" ]]; then
    echo "trace_sql_report: building report tool..."
    g++ -std=c++20 -O2 -DCHRONON3D_ENABLE_TRACING=1 \
        -Iinclude -I"$PERFETTO_SDK_DIR" \
        tools/trace_gates/trace_sql_report.cpp \
        "$CACHE/perfetto.o" -o "$BIN"
fi

"$BIN" "$TRACE_FILE" "$@"
