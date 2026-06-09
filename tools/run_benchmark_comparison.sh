#!/usr/bin/env bash
set -euo pipefail

# ──────────────────────────────────────────────────────────────────────────────
# run_benchmark_comparison.sh
#
# Runs a benchmark composition under 3 configurations and shows a comparison
# of the 6 key metrics:
#   A) Baseline (requires a separate binary, e.g. before pool-fix PR)
#   B) Scratch v2, ping-pong OFF
#   C) Scratch v2, ping-pong ON  (current default)
#
# Usage:
#   ./tools/run_benchmark_comparison.sh <composition_id> [options]
#
# Options:
#   --bin-a <path>    Path to baseline binary (A).  Default: same as --bin-c
#   --bin-b <path>    Path to ping-pong-off binary.  Default: same as --bin-c
#   --bin-c <path>    Path to current binary.  Default: auto-detect
#   --frames <N>      Measured frames per config (default: 120)
#   --warmup <N>      Warmup frames (default: 10)
#   --db <path>       Telemetry DB path.  Default: ~/.chronon3d/telemetry/*.sqlite
#   --output <path>   Output directory for reports (default: output/benchmark)
#   --help            Show this help
#
# Examples:
#   # Compare current binary with ping-pong on/off
#   ./tools/run_benchmark_comparison.sh BreathingGrid
#
#   # Compare with a baseline binary
#   ./tools/run_benchmark_comparison.sh BreathingGrid --bin-a ../chronon3d_baseline/build/apps/chronon3d_cli/chronon3d_cli
# ──────────────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Defaults ──────────────────────────────────────────────────────────────────
DEFAULT_BIN=""
FRAMES=120
WARMUP=10
TELEMETRY_DB=""
OUTPUT_DIR="$PROJECT_DIR/output/benchmark"
COMPOSITION=""

# Try to find the cli binary
find_default_bin() {
    # Common build output locations
    for candidate in \
        "$PROJECT_DIR/build/apps/chronon3d_cli/chronon3d_cli" \
        "$PROJECT_DIR/build/linux-release/apps/chronon3d_cli/chronon3d_cli" \
        "$PROJECT_DIR/build/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli" \
        "$PROJECT_DIR/build/linux-ninja/apps/chronon3d_cli/chronon3d_cli" \
        "$PROJECT_DIR/build/default/apps/chronon3d_cli/chronon3d_cli"; do
        if [[ -x "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    echo ""
    return 1
}

# Find telemetry database
find_telemetry_db() {
    local db
    db=$(ls -t "$HOME/.chronon3d/telemetry/"*.sqlite 2>/dev/null | head -1)
    echo "$db"
}

get_last_run_id() {
    local db="$1"
    if [[ ! -f "$db" ]]; then
        echo ""
        return 1
    fi
    sqlite3 "$db" "SELECT run_id FROM render_runs ORDER BY rowid DESC LIMIT 1;" 2>/dev/null || echo ""
}

# ── Parse arguments ───────────────────────────────────────────────────────────
BIN_A=""
BIN_B=""
BIN_C=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bin-a)       BIN_A="$2"; shift 2 ;;
        --bin-b)       BIN_B="$2"; shift 2 ;;
        --bin-c)       BIN_C="$2"; shift 2 ;;
        --frames)      FRAMES="$2"; shift 2 ;;
        --warmup)      WARMUP="$2"; shift 2 ;;
        --db)          TELEMETRY_DB="$2"; shift 2 ;;
        --output)      OUTPUT_DIR="$2"; shift 2 ;;
        --help)        head -30 "$0"; exit 0 ;;
        *)             COMPOSITION="$1"; shift ;;
    esac
done

if [[ -z "$COMPOSITION" ]]; then
    echo "Error: composition_id is required" >&2
    echo "Usage: $0 <composition_id> [options]" >&2
    exit 1
fi

# Resolve binaries
if [[ -z "$BIN_C" ]]; then
    BIN_C=$(find_default_bin)
    if [[ -z "$BIN_C" ]]; then
        echo "Error: cannot find chronon3d_cli binary. Build it first or specify with --bin-c" >&2
        exit 1
    fi
fi

BIN_A="${BIN_A:-$BIN_C}"
BIN_B="${BIN_B:-$BIN_C}"

# Resolve telemetry DB
if [[ -z "$TELEMETRY_DB" ]]; then
    TELEMETRY_DB=$(find_telemetry_db)
    if [[ -z "$TELEMETRY_DB" ]]; then
        echo "Error: cannot find telemetry database. Run a render/bench command first or specify with --db" >&2
        exit 1
    fi
fi

mkdir -p "$OUTPUT_DIR"

echo "══════════════════════════════════════════════════════════════════════════"
echo "  Benchmark Comparison: $COMPOSITION"
echo "  Binary A (baseline): $BIN_A"
echo "  Binary B (pingpong off): $BIN_B"
echo "  Binary C (pingpong on): $BIN_C"
echo "  Frames: $FRAMES (warmup: $WARMUP)"
echo "  Telemetry DB: $TELEMETRY_DB"
echo "  Output: $OUTPUT_DIR"
echo "══════════════════════════════════════════════════════════════════════════"
echo ""

# Make a backup of the telemetry DB
DB_BACKUP="$OUTPUT_DIR/telemetry_backup.sqlite"
cp "$TELEMETRY_DB" "$DB_BACKUP" || true
echo "  Telemetry DB backed up to $DB_BACKUP"
echo ""

run_config() {
    local label="$1"
    local bin="$2"
    local pingpong="$3"   # "1" or "0"
    local output_json="$OUTPUT_DIR/bench_${label}.json"

    echo "─── Config ${label}: $bin (CHRONON_PINGPONG_FRAMEBUFFER=${pingpong}) ───"

    # Run the bench command
    if ! CHRONON_PINGPONG_FRAMEBUFFER="$pingpong" \
        "$bin" bench "$COMPOSITION" \
        --frames "$FRAMES" \
        --warmup "$WARMUP" \
        --json "$output_json" \
        --quiet 2>&1; then
        echo "WARNING: benchmark failed for config ${label}, trying to continue" >&2
    fi

    # Get the last run_id from telemetry DB
    local run_id
    run_id=$(get_last_run_id "$TELEMETRY_DB")
    echo "  Run ID: $run_id"
    echo "$run_id" > "$OUTPUT_DIR/run_id_${label}.txt"
    echo ""
}

# ── Run all 3 configs ────────────────────────────────────────────────────────
run_config "A" "$BIN_A" "1"
run_config "B" "$BIN_B" "0"
run_config "C" "$BIN_C" "1"

# ── Compare ───────────────────────────────────────────────────────────────────
RUN_ID_A=$(cat "$OUTPUT_DIR/run_id_A.txt" 2>/dev/null || echo "")
RUN_ID_B=$(cat "$OUTPUT_DIR/run_id_B.txt" 2>/dev/null || echo "")
RUN_ID_C=$(cat "$OUTPUT_DIR/run_id_C.txt" 2>/dev/null || echo "")

if [[ -n "$RUN_ID_A" && -n "$RUN_ID_B" && -n "$RUN_ID_C" ]]; then
    echo "──────────────────────────────────────────────────────────────────────"
    echo "  Comparison Results"
    echo "──────────────────────────────────────────────────────────────────────"
    "$SCRIPT_DIR/compare_benchmarks.py" \
        "$TELEMETRY_DB" \
        "${RUN_ID_A}:A (baseline)" \
        "${RUN_ID_B}:B (pingpong off)" \
        "${RUN_ID_C}:C (pingpong on)"

    # Save comparison output
    "$SCRIPT_DIR/compare_benchmarks.py" \
        "$TELEMETRY_DB" \
        "${RUN_ID_A}:A (baseline)" \
        "${RUN_ID_B}:B (pingpong off)" \
        "${RUN_ID_C}:C (pingpong on)" \
        > "$OUTPUT_DIR/comparison.txt" 2>&1 || true
    echo ""
    echo "  Full comparison saved to: $OUTPUT_DIR/comparison.txt"
else
    echo "Warning: could not read run_ids from telemetry DB" >&2
    echo "  A: $RUN_ID_A"
    echo "  B: $RUN_ID_B"
    echo "  C: $RUN_ID_C"
fi

echo "══════════════════════════════════════════════════════════════════════════"
echo "  Done. Results in: $OUTPUT_DIR"
echo "══════════════════════════════════════════════════════════════════════════"
