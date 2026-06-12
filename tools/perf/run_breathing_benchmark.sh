#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# run_breathing_benchmark.sh — PR 0: Stable Performance Benchmark Harness
#
# Runs the MinimalistImageTrackingBreathing composition for 150 frames at
# 30 fps and saves telemetry reports with clear naming for later comparison.
#
# Usage:
#   ./tools/perf/run_breathing_benchmark.sh [options]
#
# Options:
#   --label <name>     Label for the report (default: "current")
#                      Reports saved as: reports/perf/breathing_<label>.json
#   --bin <path>       Path to chronon3d_cli binary (auto-detect if omitted)
#   --output-dir <dir> Output directory for video (default: output/perf)
#   --frames <N>       Number of frames (default: 150)
#   --fps <N>          Frames per second (default: 30)
#   --start <N>        Start frame (default: 0)
#   --help             Show this help
#
# The script:
#   1. Finds/verifies the chronon3d_cli binary
#   2. Runs the video export command
#   3. Generates a telemetry report
#   4. Saves run_id and report with clear naming
#
# Examples:
#   # Quick benchmark
#   ./tools/perf/run_breathing_benchmark.sh
#
#   # Named benchmark for comparison
#   ./tools/perf/run_breathing_benchmark.sh --label v14
#
#   # With custom binary
#   ./tools/perf/run_breathing_benchmark.sh --label v19 --bin ./build/apps/chronon3d_cli/chronon3d_cli
# ──────────────────────────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# ── Defaults ──────────────────────────────────────────────────────────────────
LABEL="current"
BIN=""
OUTPUT_DIR="$PROJECT_DIR/output/perf"
FRAMES=150
FPS=30
START=0
COMPOSITION="MinimalistImageTrackingBreathing"

# ── Parse arguments ───────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)      LABEL="$2"; shift 2 ;;
        --bin)        BIN="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --frames)     FRAMES="$2"; shift 2 ;;
        --fps)        FPS="$2"; shift 2 ;;
        --start)      START="$2"; shift 2 ;;
        --comp)       COMPOSITION="$2"; shift 2 ;;
        --help)
            head -40 "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            head -40 "$0"
            exit 1
            ;;
    esac
done

# ── Resolve binary ────────────────────────────────────────────────────────────
find_default_bin() {
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

if [[ -z "$BIN" ]]; then
    BIN=$(find_default_bin)
    if [[ -z "$BIN" ]]; then
        echo "Error: cannot find chronon3d_cli binary." >&2
        echo "Build it first or specify with --bin <path>" >&2
        exit 1
    fi
fi

if [[ ! -x "$BIN" ]]; then
    echo "Error: binary not found or not executable: $BIN" >&2
    exit 1
fi

# ── Check for required tools ──────────────────────────────────────────────────
if ! command -v sqlite3 &>/dev/null; then
    echo "Error: sqlite3 is required but not found in PATH" >&2
    exit 1
fi

# ── Compute paths ─────────────────────────────────────────────────────────────
REPORTS_DIR="$PROJECT_DIR/reports/perf"
mkdir -p "$REPORTS_DIR"
mkdir -p "$OUTPUT_DIR"

VIDEO_OUT="$OUTPUT_DIR/breathing_${LABEL}.mp4"
REPORT_JSON="$REPORTS_DIR/breathing_${LABEL}.json"
REPORT_MD="$REPORTS_DIR/breathing_${LABEL}.md"
RUN_ID_FILE="$REPORTS_DIR/breathing_${LABEL}.run_id"

echo "══════════════════════════════════════════════════════════════════════════"
echo "  Breathing Benchmark: $COMPOSITION"
echo "  Binary: $BIN"
echo "  Label:  $LABEL"
echo "  Frames: $FRAMES (start=$START, fps=$FPS)"
echo "  Output: $VIDEO_OUT"
echo "  Report: $REPORT_JSON"
echo "══════════════════════════════════════════════════════════════════════════"
echo ""

# ── Run video export ─────────────────────────────────────────────────────────
echo "─── Running video export ───"

BENCH_START=$(date +%s.%N)

set +e  # Don't exit on video failure — we still want to check telemetry
"$BIN" video "$COMPOSITION" \
    --start "$START" \
    --end "$((START + FRAMES))" \
    --fps "$FPS" \
    -o "$VIDEO_OUT" \
    2>&1
VIDEO_EXIT=$?

BENCH_END=$(date +%s.%N)
WALL_TIME=$(echo "$BENCH_END - $BENCH_START" | bc)

if [[ $VIDEO_EXIT -ne 0 ]]; then
    echo "WARNING: video export exited with code $VIDEO_EXIT" >&2
fi

echo "  Video export completed in ${WALL_TIME}s"
echo "  Video output: $VIDEO_OUT"

# ── Resolve telemetry DB ──────────────────────────────────────────────────────
find_telemetry_db() {
    # Prefer local output/telemetry.db if it exists
    if [[ -f "$PROJECT_DIR/output/telemetry.db" ]]; then
        echo "$PROJECT_DIR/output/telemetry.db"
        return 0
    fi
    # Fall back to user telemetry directory
    local db
    db=$(ls -t "$HOME/.chronon3d/telemetry/"*.sqlite 2>/dev/null | head -1)
    echo "$db"
}

TELEMETRY_DB=$(find_telemetry_db)
if [[ -z "$TELEMETRY_DB" || ! -f "$TELEMETRY_DB" ]]; then
    echo "WARNING: cannot find telemetry database" >&2
    echo "Attempted: output/telemetry.db and ~/.chronon3d/telemetry/*.sqlite" >&2
else
    echo "  Telemetry DB: $TELEMETRY_DB"

    # Get the most recent run_id from the telemetry DB
    # Guard against sqlite3 failure with || echo ""
    RUN_ID=$(sqlite3 "$TELEMETRY_DB" \
        "SELECT run_id FROM render_runs ORDER BY finished_at_iso DESC LIMIT 1;" \
        2>/dev/null || echo "")

    if [[ -n "$RUN_ID" ]]; then
        echo "  Run ID: $RUN_ID"
        echo "$RUN_ID" > "$RUN_ID_FILE"

        # Generate a telemetry report (markdown) if the telemetry subcommand exists
        echo ""
        echo "─── Generating telemetry report ───"
        "$BIN" telemetry --run-id "$RUN_ID" -o "$REPORT_MD" 2>&1 || echo "  (telemetry report generation skipped — subcommand may not be available)"

        # Generate JSON summary using Python (more portable than sqlite3 -json)
        echo "─── Saving JSON summary ───"
        if command -v python3 &>/dev/null; then
            # Use heredoc to avoid quoting issues with shell variable interpolation
            python3 - "$TELEMETRY_DB" "$RUN_ID" "$REPORT_JSON" << 'PYEOF' 2>/dev/null || echo "  WARNING: Python JSON generation failed"
import sqlite3, json, sys
if len(sys.argv) < 4:
    sys.exit(1)
db_path, run_id, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    db = sqlite3.connect(db_path)
    db.row_factory = sqlite3.Row
    row = db.execute("SELECT * FROM render_runs WHERE run_id = ?", (run_id,)).fetchone()
    if row:
        data = dict(row)
        counters = {}
        for c in db.execute("SELECT counter_name, counter_value FROM render_counters WHERE run_id = ?", (run_id,)):
            counters[c[0]] = c[1]
        data['_counters'] = counters
        with open(out_path, 'w') as f:
            json.dump(data, f, indent=2, default=str)
        print(f'  JSON summary saved to {out_path}')
    else:
        print(f'WARNING: run_id {run_id} not found in render_runs', file=sys.stderr)
    db.close()
except Exception as e:
    print(f'WARNING: could not save JSON summary: {e}', file=sys.stderr)
PYEOF
        else
            echo "  WARNING: python3 not found; skipping JSON summary"
        fi
    else
        echo "WARNING: no run_id found in telemetry DB" >&2
    fi
fi

echo ""
echo "══════════════════════════════════════════════════════════════════════════"
echo "  Benchmark complete"
echo "  Report: $REPORT_JSON"
if [[ -f "$RUN_ID_FILE" ]]; then
    echo "  Run ID: $(cat "$RUN_ID_FILE")"
fi
echo "──────────────────────────────────────────────────────────────────────────"
echo "  Next: compare with another run using:"
echo "    ./tools/perf/compare_telemetry.py <run_id_a> <run_id_b>"
echo "    ./tools/perf/compare_telemetry.py --latest-two"
echo "══════════════════════════════════════════════════════════════════════════"
