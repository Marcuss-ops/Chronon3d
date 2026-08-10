#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/bench_perf_stat.sh — perf-stat wrapper for Chronon3D (F1.3)
#
# Wraps a Chronon3D render under Linux `perf stat` with the canonical
# hardware event set from the machine-certification plan and extracts
# per-frame metrics:
#
#   cycles/frame          instructions/frame      IPC
#   cache miss %          branch miss %            context switches
#   CPU migrations        page faults
#
# 100% CPU ≠ CPU used well: cycles/frame is the KPI, not % CPU.
# perf stat provides the hardware truth; this script turns it into the
# per-frame numbers a render engine should track.
#
# Requirements:
#   - Linux with `perf stat` (linux-tools-common / linux-tools-$(uname -r))
#   - A chronon3d_cli binary with the `bench` subcommand (benchmark build)
#     OR the `render` subcommand (all builds)
#
# On VPS/containers where perf_event_open is restricted, `perf stat`
# exits with EACCES.  Fix: `sudo sysctl -w kernel.perf_event_paranoid=1`
# (or boot with `perf_event_paranoid=1`).  When perf is unavailable the
# script emits a clear diagnostic and exits 1.
#
# Usage:
#   bash tools/bench_perf_stat.sh --comp BenchB00_EmptyFrame
#   bash tools/bench_perf_stat.sh --comp BenchB03_CinematicGlow1080p --frames 120 -r 5
#   bash tools/bench_perf_stat.sh --comp BenchB00_EmptyFrame --render-mode --cli ./build/manual-test/chronon3d_cli
#
# Options:
#   --comp <id>       Composition to render (required)
#   --frames <n>      Frames per perf repetition (default 60)
#   -r <n>            perf stat repetitions (default 10, per the plan's '-r 10')
#   --render-mode     Use `chronon render --frame 0` instead of `chronon bench`
#   --warmup <n>      bench-mode warmup frames (default 10; ignored in render-mode)
#   --output <path>   Write JSON report to <path>
#   --cli <path>      chronon3d_cli binary (auto-detected)
#   --taskset <mask>  CPU affinity mask, e.g. 0-3 (default: all CPUs)
#   -h|--help         Print this help and exit
# ═══════════════════════════════════════════════════════════════════════════

set -uo pipefail

GATE_NAME="bench_perf_stat"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Defaults ─────────────────────────────────────────────────────────────
COMP=""
FRAMES=60
REPETITIONS=10
MODE_RENDER=false
WARMUP=10
OUTPUT_FILE=""
CLI_BIN=""
TASKSET_MASK=""

# ── Arg parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --comp)         COMP="$2"; shift 2 ;;
        --frames)       FRAMES="$2"; shift 2 ;;
        -r|--repetitions) REPETITIONS="$2"; shift 2 ;;
        --render-mode)  MODE_RENDER=true; shift ;;
        --warmup)       WARMUP="$2"; shift 2 ;;
        --output)       OUTPUT_FILE="$2"; shift 2 ;;
        --cli)          CLI_BIN="$2"; shift 2 ;;
        --taskset)      TASKSET_MASK="$2"; shift 2 ;;
        -h|--help)      sed -n '2,50p' "$0"; exit 0 ;;
        *) echo "GATE_FAIL: ${GATE_NAME}: unknown arg: $1" >&2; exit 2 ;;
    esac
done

# Required: composition
if [[ -z "$COMP" ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: --comp is required" >&2
    echo "  Usage: bash tools/bench_perf_stat.sh --comp <composition_id>" >&2
    exit 2
fi

# ── Auto-detect CLI binary ──────────────────────────────────────────────
if [[ -z "$CLI_BIN" ]]; then
    for candidate in \
        "$PROJECT_ROOT/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli" \
        "$PROJECT_ROOT/build/manual-test/chronon3d_cli" \
        "$PROJECT_ROOT/build/chronon/linux-content-dev/apps/chronon3d_cli/chronon3d_cli" \
        "$PROJECT_ROOT/build/chronon/linux-dev/apps/chronon3d_cli/chronon3d_cli"; do
        if [[ -x "$candidate" ]]; then CLI_BIN="$candidate"; break; fi
    done
fi
if [[ ! -x "$CLI_BIN" ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: chronon3d_cli not found" >&2
    echo "  Set --cli <path> or build first." >&2
    echo "  Candidates checked: ${PROJECT_ROOT}/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli" >&2
    exit 1
fi
echo "[INFO] ${GATE_NAME}: CLI = $CLI_BIN"

# ── Check perf availability ─────────────────────────────────────────────
if ! command -v perf &>/dev/null; then
    echo "GATE_FAIL: ${GATE_NAME}: 'perf' not found in PATH" >&2
    echo "  Install: sudo apt install linux-tools-common linux-tools-\$(uname -r)" >&2
    echo "  Or:      sudo yum install perf" >&2
    echo "  If perf is installed but not found, check PATH or set CHRONON3D_PERF=/path/to/perf" >&2
    exit 1
fi
if ! perf stat -e cycles -r 1 /bin/true &>/dev/null; then
    echo "GATE_FAIL: ${GATE_NAME}: perf stat not usable (perf_event_open blocked)" >&2
    echo "  On VPS/containers, try: sudo sysctl -w kernel.perf_event_paranoid=1" >&2
    echo "  Or set:                 sudo sh -c 'echo 1 > /proc/sys/kernel/perf_event_paranoid'" >&2
    exit 1
fi
echo "[INFO] ${GATE_NAME}: perf = $(command -v perf)"

# ── Build the chronon command ───────────────────────────────────────────
CHRONON_CMD=()
if [[ "$MODE_RENDER" == "true" ]]; then
    # Render-mode: single frame render (no file I/O overhead difference between frames)
    # Perf stat will repeat the whole render $REPETITIONS times.
    TMP_PNG=$(mktemp /tmp/chronon_perf_XXXXXX.png)
    trap 'rm -f "$TMP_PNG"' EXIT
    CHRONON_CMD=("$CLI_BIN" render "$COMP" --frame 0 --output "$TMP_PNG" --log-level error)
    echo "[INFO] ${GATE_NAME}: mode = render --frame 0 (single frame, $REPETITIONS repetitions)"
else
    # Bench-mode: uses the canonical benchmark subcommand (requires benchmark build).
    CHRONON_CMD=("$CLI_BIN" bench "$COMP" --frames "$FRAMES" --warmup "$WARMUP" --quiet)
    echo "[INFO] ${GATE_NAME}: mode = bench --frames $FRAMES --warmup $WARMUP ($REPETITIONS repetitions)"
fi

# ── Taskset prefix ──────────────────────────────────────────────────────
TASKSET_PREFIX=()
if [[ -n "$TASKSET_MASK" ]]; then
    if command -v taskset &>/dev/null; then
        TASKSET_PREFIX=(taskset -c "$TASKSET_MASK")
        echo "[INFO] ${GATE_NAME}: taskset = $TASKSET_MASK"
    else
        echo "[WARN] ${GATE_NAME}: taskset not available, --taskset ignored" >&2
    fi
fi

# ── Perf events (canonical set from the machine-certification plan) ─────
PERF_EVENTS="cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,cpu-migrations,page-faults"

# ── Run perf stat ───────────────────────────────────────────────────────
PERF_LOG=$(mktemp /tmp/chronon_perf_XXXXXX.stat)
trap 'rm -f "$PERF_LOG" "$TMP_PNG"' EXIT

echo "[INFO] ${GATE_NAME}: perf stat -r $REPETITIONS -e $PERF_EVENTS ${CHRONON_CMD[@]}"
PERF_START=$(date +%s%N)
if ! "${TASKSET_PREFIX[@]}" perf stat -r "$REPETITIONS" -o "$PERF_LOG" -e "$PERF_EVENTS" \
        "${CHRONON_CMD[@]}" >/dev/null 2>&1; then
    echo "GATE_FAIL: ${GATE_NAME}: perf stat exited non-zero (rc=$?)" >&2
    echo "  Command: ${TASKSET_PREFIX[@]} perf stat -r $REPETITIONS -e $PERF_EVENTS ${CHRONON_CMD[@]}" >&2
    echo "  perf output:" >&2
    cat "$PERF_LOG" >&2
    exit 3
fi
PERF_END=$(date +%s%N)
PERF_ELAPSED_MS=$(( (PERF_END - PERF_START) / 1000000 ))

# ── Parse perf stat output ──────────────────────────────────────────────
# Format: columns are count (with commas), optional ±stddev, event name.
# We extract the mean count (token 1) skipping the ± part.
parse_perf_event() {
    local event_name="$1"
    # Match the event name as a word, followed by whitespace or comment.
    # perf stat output format: <count> <event> [optional # comment] [trailing spaces]
    local line
    line=$(grep -E "[0-9,]+[[:space:]]+${event_name}[[:space:]#]" "$PERF_LOG" | head -1)
    if [[ -z "$line" ]]; then
        echo "N/A"
        return
    fi
    # Extract the first whitespace-separated token (the count with commas).
    local raw_count
    read -r raw_count _ <<< "$line"
    # Remove commas.
    echo "${raw_count//,/}"
}

# ── Extract metrics ─────────────────────────────────────────────────────
CYCLES=$(parse_perf_event "cycles")
INSTRS=$(parse_perf_event "instructions")
BRANCHES=$(parse_perf_event "branches")
BRANCH_MISS=$(parse_perf_event "branch-misses")
CACHE_REF=$(parse_perf_event "cache-references")
CACHE_MISS=$(parse_perf_event "cache-misses")
CTX_SW=$(parse_perf_event "context-switches")
CPU_MIG=$(parse_perf_event "cpu-migrations")
PAGE_FLT=$(parse_perf_event "page-faults")

# Number of frames per repetition (used for per-frame metrics).
# In render-mode: 1 frame per repetition.
# In bench-mode: FRAMES frames per repetition.
if [[ "$MODE_RENDER" == "true" ]]; then
    FRAMES_PER_RUN=1
else
    FRAMES_PER_RUN=$FRAMES
fi

# ── Compute derived metrics ─────────────────────────────────────────────
compute_div() {
    local num="$1" den="$2"
    if [[ "$num" == "N/A" || "$den" == "N/A" || "$den" -eq 0 ]] 2>/dev/null; then
        echo "N/A"
        return
    fi
    awk -v n="$num" -v d="$den" 'BEGIN{printf "%.2f", n / d}'
}

compute_pct() {
    local num="$1" den="$2"
    if [[ "$num" == "N/A" || "$den" == "N/A" || "$den" -eq 0 ]] 2>/dev/null; then
        echo "N/A"
        return
    fi
    awk -v n="$num" -v d="$den" 'BEGIN{printf "%.2f", 100.0 * n / d}'
}

format_num() {
    local val="$1"
    if [[ "$val" == "N/A" ]]; then
        echo "N/A"
        return
    fi
    # Format with commas.
    awk -v n="$val" 'BEGIN{
        s = sprintf("%.0f", n);
        while (match(s, /[0-9][0-9][0-9][0-9]/)) {
            s = substr(s, 1, RLENGTH - 3) "," substr(s, RLENGTH - 2);
        }
        print s
    }'
}

CYCLES_FRAME=$(compute_div "$CYCLES" "$FRAMES_PER_RUN")
INSTRS_FRAME=$(compute_div "$INSTRS" "$FRAMES_PER_RUN")
IPC=$(compute_div "$INSTRS" "$CYCLES")
CACHE_MISS_PCT=$(compute_pct "$CACHE_MISS" "$CACHE_REF")
BRANCH_MISS_PCT=$(compute_pct "$BRANCH_MISS" "$BRANCHES")

# ── Build report ────────────────────────────────────────────────────────
report() {
    local fmt
    if [[ "$1" == "json" ]]; then
        fmt="json"
    else
        fmt="text"
    fi

    if [[ "$fmt" == "text" ]]; then
        echo ""
        echo "CHRONON3D PERF STAT REPORT"
        echo "=================================================="
        echo "SCENE"
        echo "Composition................. $COMP"
        echo "Frames per repetition....... $FRAMES_PER_RUN"
        echo "perf repetitions............ $REPETITIONS"
        echo "Measured elapsed............ ${PERF_ELAPSED_MS} ms"
        echo ""
        echo "HARDWARE (per perf stat, mean of $REPETITIONS runs)"
        echo "Cycles/frame................ $(format_num "$CYCLES_FRAME")"
        echo "Instructions/frame.......... $(format_num "$INSTRS_FRAME")"
        echo "IPC......................... $IPC"
        echo "Cache miss rate............. ${CACHE_MISS_PCT}%"
        echo "Branch miss rate............ ${BRANCH_MISS_PCT}%"
        echo "Context switches............ $CTX_SW"
        echo "CPU migrations.............. $CPU_MIG"
        echo "Page faults................. $PAGE_FLT"
        echo ""
        echo "ROW DATA"
        echo "Cycles (total).............. $(format_num "$CYCLES")"
        echo "Instructions (total)........ $(format_num "$INSTRS")"
        echo "Branches.................... $(format_num "$BRANCHES")"
        echo "Branch misses............... $(format_num "$BRANCH_MISS")"
        echo "Cache references............ $(format_num "$CACHE_REF")"
        echo "Cache misses................ $(format_num "$CACHE_MISS")"
        echo "=================================================="
    else
        # JSON output
        cat <<JSONEOF
{
  "composition": "$COMP",
  "frames_per_run": $FRAMES_PER_RUN,
  "repetitions": $REPETITIONS,
  "elapsed_ms": $PERF_ELAPSED_MS,
  "perf_events": {
    "cycles": "$CYCLES",
    "instructions": "$INSTRS",
    "branches": "$BRANCHES",
    "branch_misses": "$BRANCH_MISS",
    "cache_references": "$CACHE_REF",
    "cache_misses": "$CACHE_MISS",
    "context_switches": "$CTX_SW",
    "cpu_migrations": "$CPU_MIG",
    "page_faults": "$PAGE_FLT"
  },
  "derived": {
    "cycles_per_frame": "$CYCLES_FRAME",
    "instructions_per_frame": "$INSTRS_FRAME",
    "ipc": "$IPC",
    "cache_miss_pct": "$CACHE_MISS_PCT",
    "branch_miss_pct": "$BRANCH_MISS_PCT"
  }
}
JSONEOF
    fi
}

# ── Print report ────────────────────────────────────────────────────────
report text

if [[ -n "$OUTPUT_FILE" ]]; then
    report json > "$OUTPUT_FILE"
    echo "[INFO] ${GATE_NAME}: JSON report written to $OUTPUT_FILE"
fi

echo "[INFO] ${GATE_NAME}: PASS (composition=$COMP frames=$FRAMES_PER_RUN repetitions=$REPETITIONS elapsed=${PERF_ELAPSED_MS}ms)"
exit 0