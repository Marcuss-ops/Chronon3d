#!/usr/bin/env bash
# ============================================================================
# tools/check_trace_correctness.sh — plan §23 correctness gate (opt-in)
# ============================================================================
#
# A 10-frame render through the REAL tracing machinery (TraceSession with
# RING_BUFFER + job-end write, CHRONON_TRACE_FLOW_IDS/FLOW_END_IDS macros,
# MakeFlowId(job, frame)) must produce a .pftrace with:
#   - 10 "DecodeFrame" slices (chronon.media,  non-terminating flow)
#   - 10 "RenderFrame" slices (chronon.frame,  non-terminating flow)
#   - 10 "EncodeFrame" slices (chronon.encode, terminating flow)
#   - the same flow id chaining decode -> render -> encode per frame
#   - no missing frame_id across the three stages (0..9 all present)
#
# The harness (tools/trace_gates/trace_correctness_check.cpp) emits the
# three stages from three threads, then re-reads the produced trace with the
# Perfetto pbzero decoders shipped in the SDK header and asserts the
# structure — no trace_processor / no UI needed.
#
# Opt-in semantics (mirrors tools/check_clean_rebuild.sh): NOT in the
# standard pre-push chain — the first build compiles the Perfetto
# amalgamation (~minutes).  Activation signal: CHRONON3D_TRACE_GATES=1.
# The Perfetto amalgamation (perfetto.h + perfetto.cc) must be available
# via PERFETTO_SDK_DIR (default: /tmp/perfetto_sdk); the compiled
# perfetto.o is cached across runs.
#
# Env vars:
#   CHRONON3D_TRACE_GATES           = 1 to opt in (default: unset -> no-op)
#   PERFETTO_SDK_DIR                = dir with perfetto.h + perfetto.cc
#   CHRONON3D_TRACE_GATE_CACHE      = build cache dir (default: /tmp/chronon3d_trace_gates)
#
# Exit codes:
#   0 = GATE_PASS (or opt-out no-op)
#   1 = GATE_FAIL (trace structure violated)
#   2 = internal error (missing perfetto amalgamation, build failure)
# ============================================================================
set -euo pipefail
GATE_NAME=check_trace_correctness

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ "${CHRONON3D_TRACE_GATES:-}" != "1" ]]; then
    echo "[INFO] ${GATE_NAME}: opt-in gate not activated (set CHRONON3D_TRACE_GATES=1)"
    exit 0
fi

PERFETTO_SDK_DIR="${PERFETTO_SDK_DIR:-/tmp/perfetto_sdk}"
CACHE="${CHRONON3D_TRACE_GATE_CACHE:-/tmp/chronon3d_trace_gates}"
BIN="$CACHE/trace_correctness_check"

if [[ ! -f "$PERFETTO_SDK_DIR/perfetto.h" || ! -f "$PERFETTO_SDK_DIR/perfetto.cc" ]]; then
    echo "GATE_FAIL: Perfetto amalgamation not found in $PERFETTO_SDK_DIR" >&2
    echo "  (set PERFETTO_SDK_DIR to a dir containing perfetto.h + perfetto.cc)" >&2
    exit 2
fi

mkdir -p "$CACHE"

# One-time compile of the amalgamation (cached; ~1-2 min first time).
if [[ ! -f "$CACHE/perfetto.o" ]]; then
    echo "STEP: compiling perfetto amalgamation (first run only)..."
    g++ -std=c++20 -O2 -c "$PERFETTO_SDK_DIR/perfetto.cc" -o "$CACHE/perfetto.o"
fi

echo "STEP: building correctness harness..."
g++ -std=c++20 -O2 -DCHRONON3D_ENABLE_TRACING=1 \
    -Iinclude -I"$PERFETTO_SDK_DIR" \
    tools/trace_gates/trace_correctness_check.cpp \
    src/core/tracing/trace_session.cpp \
    src/core/tracing/tracing_categories.cpp \
    src/core/tracing/perfetto_backend.cpp \
    "$CACHE/perfetto.o" -o "$BIN" -lpthread -ldl

echo "STEP: running 10-frame trace structure check..."
"$BIN"
rc=$?
if [[ "$rc" -eq 0 ]]; then
    echo "[INFO] ${GATE_NAME}: 10-frame trace structure verified"
fi
exit "$rc"
