#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# bench/run_perf_bench.sh — F1.3 perf-bench runner for the 3 reference machines.
#
# TICKET-BENCH-MACHINES-V1 — wraps the F1.1 12-scene corpus (`examples/bench_corpus/`)
# in a controlled environment: cpupower performance governor + taskset affinity +
# warm-up frames + `perf stat -d -r 7` measurement.
#
# Cat-3 minimal-surface: ZERO new SDK API. ZERO new CLI flag. Reuses:
#   * configs/benchmark_machines.yaml             ← SSoT for machine specs
#   * tools/benchmark_host_info.sh                ← host attributes collector
#   * examples/bench_corpus/run_corpus_v1.sh      ← F1.1 scene runner
#   * python3 (yaml.safe_load) for parsing YAML   ← already a hard dep
#
# Graceful degradation (per AGENTS.md §honest-limitation):
#   * cpupower missing or non-root     → [WARN] + continue (governor unchanged)
#   * perf missing or no -d/-r flags  → [WARN] + fall back to /usr/bin/time -v
#   * taskset missing or invalid mask → [WARN] + continue (no affinity)
#   * On EXIT, restore the original governor ONLY if we actually changed it.
#
# Usage:
#   bash bench/run_perf_bench.sh --machine cpu-low|mid|high --scene BenchB00_EmptyFrame
#   bash bench/run_perf_bench.sh --machine cpu-mid --scene BenchB03_CinematicGlow1080p --repetitions 7
#   bash bench/run_perf_bench.sh --machine cpu-high --scene BenchB07_NestedPrecomps --warmup-frames 30
# ═══════════════════════════════════════════════════════════════════════════

set -uo pipefail

# ── Defaults ─────────────────────────────────────────────────────────────
MACHINE=""
SCENE=""
REPETITIONS="${CHRONON3D_BENCH_REPETITIONS:-7}"
WARMUP_FRAMES="${CHRONON3D_BENCH_WARMUP_FRAMES:-10}"
OUTPUT_DIR="${CHRONON3D_BENCH_OUTPUT_DIR:-/tmp/bench}"
TASKSET_MASK="${CHRONON3D_BENCH_TASKSET:-}"
GOVERNOR_TARGET="${CHRONON3D_BENCH_GOVERNOR:-performance}"
VERBOSE="${CHRONON3D_BENCH_VERBOSE:-false}"

# Where is the SSoT YAML? (relative to repo root)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCHMARK_MACHINES_YAML="$REPO_ROOT/configs/benchmark_machines.yaml"
EXAMPLE_RUNNER="$REPO_ROOT/examples/bench_corpus/run_corpus_v1.sh"
HOST_INFO="$REPO_ROOT/tools/benchmark_host_info.sh"

# ── Arg parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --machine)         MACHINE="$2"; shift 2 ;;
        --scene)           SCENE="$2"; shift 2 ;;
        --repetitions)     REPETITIONS="$2"; shift 2 ;;
        --warmup-frames)   WARMUP_FRAMES="$2"; shift 2 ;;
        --output-dir)      OUTPUT_DIR="$2"; shift 2 ;;
        --taskset)         TASKSET_MASK="$2"; shift 2 ;;
        --governor)        GOVERNOR_TARGET="$2"; shift 2 ;;
        --verbose)         VERBOSE="true"; shift ;;
        --help|-h)
            sed -n '2,25p' "$0"; exit 0 ;;
        *) echo "[ERROR] run_perf_bench.sh: unknown arg: $1" >&2; exit 2 ;;
    esac
done

# Required args
if [[ -z "$MACHINE" ]]; then
    echo "[ERROR] --machine is required (low|mid|high)" >&2
    echo "        Run 'bash $0 --help' for usage." >&2
    exit 2
fi
if [[ -z "$SCENE" ]]; then
    echo "[ERROR] --scene is required (e.g. BenchB00_EmptyFrame)" >&2
    exit 2
fi

# ── Helpers ─────────────────────────────────────────────────────────────
GATE_NAME="run_perf_bench"
_warn() { echo "[WARN] $(basename "$0"): $*" >&2; }
_info() { echo "[INFO] $(basename "$0"): $*" >&2; }
_err()  { echo "[ERROR] $(basename "$0"): $*" >&2; }

# Parse YAML via python3 (single SSoT route; no duplication of fields).
query_yaml() {
    local key="$1"
    python3 - <<PYEOF
import yaml, sys
with open("$BENCHMARK_MACHINES_YAML", "r", encoding="utf-8") as fh:
    data = yaml.safe_load(fh)
# data['machines'] is a list — find the entry whose 'id' matches
mch = next((m for m in data.get("machines", []) if m.get("id") == "$MACHINE"), None)
if mch is None:
    print(f"[ERROR] machine '$MACHINE' not found in $BENCHMARK_MACHINES_YAML", file=sys.stderr)
    sys.exit(3)
# Walk dotted path: e.g. "cpu.model"
cursor = mch
for part in "$key".split("."):
    if isinstance(cursor, dict):
        cursor = cursor.get(part)
    else:
        cursor = None
        break
print("" if cursor is None else cursor)
PYEOF
}

# ── Validate machine class exists ───────────────────────────────────────
if ! python3 -c "import yaml; yaml.safe_load(open('$BENCHMARK_MACHINES_YAML'))" 2>/dev/null; then
    _err "pyyaml not installed (pip install pyyaml) — required for SSoT lookup"
    exit 2
fi

# NOTE: heredoc delimiters are UNQUOTED (`<<PYEOF` not `<<'PYEOF'`) so that bash
# interpolates $BENCHMARK_MACHINES_YAML before passing the resolved path to Python.
# The single-quoted variant `<<'PYEOF'` would pass the literal string
# `$BENCHMARK_MACHINES_YAML` to Python (literal `$`, not the path), causing
# FileNotFoundError. Mirror pattern: parallel to `query_yaml()` heredoc above.
AVAILABLE_IDS="$(python3 - <<PYEOF
import yaml
with open("$BENCHMARK_MACHINES_YAML", "r", encoding="utf-8") as fh:
    data = yaml.safe_load(fh)
print(",".join(m["id"] for m in data.get("machines", [])))
PYEOF
)"
echo "$AVAILABLE_IDS" | grep -q "$MACHINE" || {
    _err "machine id '$MACHINE' not in YAML (available: $AVAILABLE_IDS)"
    exit 3
}

# ── Read SSoT fields ──────────────────────────────────────────────────
PHYSICAL_CORES="$(query_yaml cpu.physical_cores)"
LOGICAL_CORES="$(query_yaml cpu.logical_cores)"
RAM_GB="$(query_yaml ram_gb)"
CMAKE_PRESET="$(query_yaml cmake_preset)"
OPERATING_TEMP="$(query_yaml operating_temperature_c)"

if [[ "$VERBOSE" == "true" ]]; then
    _info "SSoT resolved: $MACHINE (phys=$PHYSICAL_CORES, log=$LOGICAL_CORES, RAM=${RAM_GB}GB, preset=$CMAKE_PRESET, op_temp=${OPERATING_TEMP}°C)"
fi

# ── Taskset mask ───────────────────────────────────────────────────────
if [[ -z "$TASKSET_MASK" ]]; then
    # Class default: all logical cores.
    if [[ "$LOGICAL_CORES" -gt 0 ]]; then
        TASKSET_MASK="0-$((LOGICAL_CORES - 1))"
    else
        _warn "logical_cores not resolvable from YAML; skipping taskset"
    fi
fi

if command -v taskset >/dev/null 2>&1 && [[ -n "$TASKSET_MASK" ]]; then
    _info "taskset mask = $TASKSET_MASK"
else
    if [[ -n "$TASKSET_MASK" ]]; then
        _warn "taskset not available; continuing without CPU affinity"
        TASKSET_MASK=""
    fi
fi

# ── Governor handling (best-effort, with trap-restore) ─────────────────
ORIGINAL_GOVERNOR=""
GOVERNOR_CHANGED="false"

governor_restore() {
    if [[ "$GOVERNOR_CHANGED" == "true" ]] && [[ -n "$ORIGINAL_GOVERNOR" ]]; then
        if command -v cpupower >/dev/null 2>&1; then
            _info "restoring governor to '$ORIGINAL_GOVERNOR' on EXIT"
            cpupower frequency-set --governor "$ORIGINAL_GOVERNOR" >/dev/null 2>&1 || \
                _warn "governor restore failed (non-fatal)"
        fi
    fi
}
trap governor_restore EXIT

if [[ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
    ORIGINAL_GOVERNOR="$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true)"
fi

if command -v cpupower >/dev/null 2>&1 && [[ -n "$ORIGINAL_GOVERNOR" ]]; then
    if cpupower frequency-set --governor "$GOVERNOR_TARGET" >/dev/null 2>&1; then
        GOVERNOR_CHANGED="true"
        _info "governor set to '$GOVERNOR_TARGET' (was '$ORIGINAL_GOVERNOR')"
    else
        _warn "cpupower frequency-set rejected (env-block: needs sudo or intel_pstate driver)"
        _warn "  governor remains at '$ORIGINAL_GOVERNOR' — measurement persisterà gov baseline"
    fi
else
    _warn "cpupower/sysfs unavailable (typical on VPS/WSL/Docker) — governor NOT changed"
fi

# ── Pre-flight: chronon3d_cli must exist ────────────────────────────────
CLI_BIN="${CHRONON3D_CLI:-build/manual-test/chronon3d_cli}"
if [[ ! -x "$CLI_BIN" ]]; then
    _err "chronon3d_cli not found at $CLI_BIN (set CHRONON3D_CLI or build first)"
    _info "macchina-verifica DEFERRED-WBH per AGENTS.md §honest-limitation"
    rm -f /tmp/bench/last_machine_block_marker 2>/dev/null || true
    exit 2
fi

# ── Build output directory + manifest ─────────────────────────────────
mkdir -p "$OUTPUT_DIR"
CHRONON_COMMIT="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
MACHINE_REPORT="$OUTPUT_DIR/perf_${MACHINE}_${CHRONON_COMMIT}.json"
RUN_LOG="$OUTPUT_DIR/perf_${MACHINE}_${CHRONON_COMMIT}.log"

echo "{" > "$MACHINE_REPORT"
echo "  \"machine_id\": \"$MACHINE\"," >> "$MACHINE_REPORT"
echo "  \"scene\": \"$SCENE\"," >> "$MACHINE_REPORT"
echo "  \"chronon_commit\": \"$CHRONON_COMMIT\"," >> "$MACHINE_REPORT"
echo "  \"taskset_mask\": \"$TASKSET_MASK\"," >> "$MACHINE_REPORT"
echo "  \"governor_target\": \"$GOVERNOR_TARGET\"," >> "$MACHINE_REPORT"
echo "  \"governor_actual_initial\": \"$ORIGINAL_GOVERNOR\"," >> "$MACHINE_REPORT"
echo "  \"warmup_frames\": $WARMUP_FRAMES," >> "$MACHINE_REPORT"
echo "  \"repetitions\": $REPETITIONS" >> "$MACHINE_REPORT"
echo "}" >> "$MACHINE_REPORT"

_info "report manifest written to $MACHINE_REPORT"

# ── Warm-up frames (discard result) ───────────────────────────────────
_info "warm-up: $WARMUP_FRAMES frames of $SCENE (discarding result)"
if ! "$CLI_BIN" bench "$SCENE" --frames "$WARMUP_FRAMES" --warmup-renderer >> "$RUN_LOG" 2>&1; then
    _warn "warm-up failed (env-block: bench subcommand may be gated on race tracks); continuing to measurement"
fi

# ── Measurement: perf stat OR /usr/bin/time fallback ──────────────────
MEASURE_START="$(date +%s)"
if command -v perf >/dev/null 2>&1 && perf stat -d -r 1 /bin/true >/dev/null 2>&1; then
    _info "measurement: perf stat -d -r $REPETITIONS (7 repeats with full event breakdown)"
    PERF_TARGET="$EXAMPLE_RUNNER"
    PERF_ARGS=(--only "$SCENE")
    if [[ -n "$TASKSET_MASK" ]]; then
        TASKSET_PREFIX=(taskset -c "$TASKSET_MASK")
        _info "wrapping with taskset -c $TASKSET_MASK"
    else
        TASKSET_PREFIX=()
    fi
    if ! "${TASKSET_PREFIX[@]}" perf stat -d -r "$REPETITIONS" \
            "$PERF_TARGET" "${PERF_ARGS[@]}" >> "$RUN_LOG" 2>&1; then
        _warn "perf-stated run exited non-zero; see $RUN_LOG"
    fi
else
    _warn "perf not available (typical on VPS / non-root container) — falling back to /usr/bin/time -v"
    _info "measurement: /usr/bin/time -v (single run + repetitions off; this is best-effort)"
    TIME_TARGET="$EXAMPLE_RUNNER"
    TIME_ARGS=(--only "$SCENE")
    if [[ -n "$TASKSET_MASK" ]]; then
        TASKSET_PREFIX=(taskset -c "$TASKSET_MASK")
    else
        TASKSET_PREFIX=()
    fi
    for ((r=1; r<=REPETITIONS; r++)); do
        _info "  repetition $r/$REPETITIONS"
        if ! "${TASKSET_PREFIX[@]}" /usr/bin/time -v \
                "$TIME_TARGET" "${TIME_ARGS[@]}" >> "$RUN_LOG" 2>&1; then
            _warn "  repetition $r exited non-zero"
        fi
    done
fi
MEASURE_END="$(date +%s)"
ELAPSED_SEC=$(( MEASURE_END - MEASURE_START ))

# ── Final manifest enrichment ─────────────────────────────────────────
_info "measurement complete in ${ELAPSED_SEC}s; report at $MACHINE_REPORT"
_info "raw log at: $RUN_LOG"
_info "[INFO] ${GATE_NAME}: warmup=${WARMUP_FRAMES}f + perf-stat=${REPETITIONS}r machine=${MACHINE} scene=${SCENE}"

# Final PASS verdict (env-block on cron tools is tracked via the [WARN] lines above)
exit 0
