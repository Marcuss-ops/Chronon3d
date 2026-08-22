#!/usr/bin/env bash
# ============================================================================
# tools/check_trace_overhead.sh — plan §23 overhead gates (opt-in)
# ============================================================================
#
# Three percentage gates measured against a tracing-OFF baseline binary of
# the IDENTICAL 200-frame loop (tools/trace_gates/trace_overhead_bench.cpp):
#
#   build  runtime  gate
#   OFF    —        baseline
#   ON     off      <0.5%   (compile-in cost with no active session)
#   ON     pipeline <2%     (normal categories active)
#   ON     nodes    <5%     (normal + debug/slow node categories active)
#
# The harness compiles the same source with and without
# CHRONON3D_ENABLE_TRACING; each config is run best-of-3 and the minimum is
# used.  All per-frame work (CPU-bound busy loop + frame/graph/node slices +
# counter) is identical across configs, so the delta is exactly the tracing
# cost.
#
# Opt-in semantics (mirrors tools/check_clean_rebuild.sh): NOT in the
# standard pre-push chain — first build compiles the Perfetto amalgamation
# and the timing runs take ~30 s.  Activation: CHRONON3D_TRACE_GATES=1.
# Requires the Perfetto amalgamation via PERFETTO_SDK_DIR (default
# /tmp/perfetto_sdk); perfetto.o and both bench binaries are cached.
#
# Env vars:
#   CHRONON3D_TRACE_GATES           = 1 to opt in (default: unset -> no-op)
#   PERFETTO_SDK_DIR                = dir with perfetto.h + perfetto.cc
#   CHRONON3D_TRACE_GATE_CACHE      = build cache dir (default: /tmp/chronon3d_trace_gates)
#   CHRONON3D_TRACE_GATE_RUNS       = best-of-N runs per config (default: 3)
#
# Exit codes:
#   0 = all gates PASS (or opt-out no-op)
#   1 = at least one overhead gate FAIL
#   2 = internal error (missing amalgamation, build failure)
# ============================================================================
set -euo pipefail
GATE_NAME=check_trace_overhead

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ "${CHRONON3D_TRACE_GATES:-}" != "1" ]]; then
    echo "[INFO] ${GATE_NAME}: opt-in gate not activated (set CHRONON3D_TRACE_GATES=1)"
    exit 0
fi

PERFETTO_SDK_DIR="${PERFETTO_SDK_DIR:-/tmp/perfetto_sdk}"
CACHE="${CHRONON3D_TRACE_GATE_CACHE:-/tmp/chronon3d_trace_gates}"
RUNS="${CHRONON3D_TRACE_GATE_RUNS:-3}"
BENCH_SRC="tools/trace_gates/trace_overhead_bench.cpp"
BIN_OFF="$CACHE/trace_overhead_off"
BIN_ON="$CACHE/trace_overhead_on"

if [[ ! -f "$PERFETTO_SDK_DIR/perfetto.h" || ! -f "$PERFETTO_SDK_DIR/perfetto.cc" ]]; then
    echo "GATE_FAIL: Perfetto amalgamation not found in $PERFETTO_SDK_DIR" >&2
    echo "  (set PERFETTO_SDK_DIR to a dir containing perfetto.h + perfetto.cc)" >&2
    exit 2
fi

mkdir -p "$CACHE"

if [[ ! -f "$CACHE/perfetto.o" ]]; then
    echo "STEP: compiling perfetto amalgamation (first run only)..."
    g++ -std=c++20 -O2 -c "$PERFETTO_SDK_DIR/perfetto.cc" -o "$CACHE/perfetto.o"
fi

echo "STEP: building baseline (tracing OFF) + tracing binaries..."
g++ -std=c++20 -O2 -Iinclude -I"$PERFETTO_SDK_DIR" \
    "$BENCH_SRC" -o "$BIN_OFF"
g++ -std=c++20 -O2 -DCHRONON3D_ENABLE_TRACING=1 \
    -Iinclude -I"$PERFETTO_SDK_DIR" \
    "$BENCH_SRC" \
    src/core/tracing/trace_session.cpp \
    src/core/tracing/tracing_categories.cpp \
    src/core/tracing/perfetto_backend.cpp \
    "$CACHE/perfetto.o" -o "$BIN_ON" -lpthread -ldl

# best-of-N runner: prints the minimum ms for `bin [args...]`.
run_best() {
    local bin="$1"; shift
    local best=""
    local i t
    for ((i = 0; i < RUNS; ++i)); do
        t="$("$bin" "$@" | sed -n 's/^ms=//p')"
        if [[ -z "$t" ]]; then
            echo "GATE_FAIL: benchmark produced no ms= output" >&2
            exit 1
        fi
        if [[ -z "$best" ]] || awk -v a="$t" -v b="$best" 'BEGIN{exit !(a < b)}'; then
            best="$t"
        fi
    done
    printf '%s' "$best"
}

echo "STEP: timing baseline (tracing OFF)..."
off="$(run_best "$BIN_OFF")"
echo "STEP: timing tracing build ON / runtime OFF..."
on_off="$(run_best "$BIN_ON" off)"
echo "STEP: timing tracing build ON / pipeline..."
on_pipe="$(run_best "$BIN_ON" pipeline)"
echo "STEP: timing tracing build ON / nodes..."
on_nodes="$(run_best "$BIN_ON" nodes)"

# overhead% = (on - off) / off * 100 (sign included: %+.2f)
pct() {
    awk -v a="$1" -v b="$2" 'BEGIN{ printf "%+.2f", (a - b) / b * 100.0 }'
}
over_off="$(pct "$on_off" "$off")"
over_pipe="$(pct "$on_pipe" "$off")"
over_nodes="$(pct "$on_nodes" "$off")"

failures=0
check_gate() {
    local label="$1" value="$2" threshold="$3"
    if awk -v v="$value" -v t="$threshold" 'BEGIN{exit !(v < t)}'; then
        echo "  PASS ${label}: ${value}% (threshold <${threshold}%)"
    else
        echo "  FAIL ${label}: ${value}% (threshold <${threshold}%)" >&2
        failures=$((failures + 1))
    fi
}

echo "Overhead vs tracing-OFF baseline:"
check_gate "trace build-ON runtime-OFF" "$over_off" "0.5"
check_gate "trace pipeline"             "$over_pipe" "2"
check_gate "trace nodes"                "$over_nodes" "5"

if [[ "$failures" -ne 0 ]]; then
    echo "GATE_FAIL: $failures overhead gate(s) exceeded" >&2
    exit 1
fi

echo "GATE_PASS: all three overhead gates within budget"
echo "[INFO] ${GATE_NAME}: off=${over_off}% pipeline=${over_pipe}% nodes=${over_nodes}% (baseline ${off} ms)"
exit 0
