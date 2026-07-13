#!/usr/bin/env bash
# tools/measure_cpu_budget.sh — Measure render/encode CPU budget timing
#
# Measures: tempo render puro, tempo encode, utilizzo CPU, RAM, frame/minuto
# for the unified CpuBudget pipeline (post-WritePool removal).
#
# Prerequisites:
#   - Working build host with chronon3d_cli binary built
#   - ffmpeg installed
#   - /usr/bin/time available (GNU time)
#   - awk available (universal on Linux)
#
# Usage:
#   bash tools/measure_cpu_budget.sh [BINARY_PATH] [COMPOSITION_ID] [FRAMES]
#
# Defaults:
#   BINARY_PATH   = build/chronon/linux-content-dev/apps/chronon3d_cli/chronon3d_cli
#   COMPOSITION   = ChrononGlowFinalAE
#   FRAMES        = 60
#
# Output:
#   docs/baselines/cpu-budget-<timestamp>.txt  (raw metrics)
#   stdout: summary table
#
# Environment:
#   The script measures under DEFAULT CpuBudget (desktop preset).
#   To measure with explicit thread counts, set before running:
#     CHRONON3D_CPU_RENDER_THREADS=12
#     CHRONON3D_CPU_DECODE_THREADS=2
#     CHRONON3D_CPU_ENCODE_THREADS=2
#     CHRONON3D_CPU_MACHINE_CLASS=desktop|laptop|server|embedded

set -euo pipefail

GATE_NAME=measure_cpu_budget
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BINARY="${1:-${PROJECT_ROOT}/build/chronon/linux-content-dev/apps/chronon3d_cli/chronon3d_cli}"
COMPOSITION="${2:-ChrononGlowFinalAE}"
FRAMES="${3:-60}"
OUTPUT_DIR="${PROJECT_ROOT}/output/cpu-budget-measure"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
BASELINE_FILE="${PROJECT_ROOT}/docs/baselines/cpu-budget-${TIMESTAMP}.txt"

# ── Pre-flight checks ──────────────────────────────────────────────
if [ ! -x "$BINARY" ]; then
    echo "GATE_FAIL: ${GATE_NAME}: chronon3d_cli not found at $BINARY"
    echo "  Build first: cmake --build build/chronon/linux-content-dev --target chronon3d_cli -j\$(nproc)"
    exit 1
fi

if ! command -v ffmpeg &>/dev/null; then
    echo "GATE_FAIL: ${GATE_NAME}: ffmpeg not found"
    exit 1
fi

if ! command -v /usr/bin/time &>/dev/null; then
    echo "GATE_FAIL: ${GATE_NAME}: /usr/bin/time (GNU time) not found"
    exit 1
fi

mkdir -p "$OUTPUT_DIR" "$(dirname "$BASELINE_FILE")"

# ── Helper: extract timing from /usr/bin/time -v output ────────────
extract_time_metric() {
    local log_file="$1"
    local metric_name="$2"
    # /usr/bin/time -v outputs:
    #   Elapsed (wall clock) time (h:mm:ss or m:ss): 0:03.42
    #   Percent of CPU this job got: 123%
    #   Maximum resident set size (kbytes): 123456
    grep "$metric_name" "$log_file" 2>/dev/null | awk '{print $NF}' || echo "N/A"
}

# Convert wall clock time (m:ss.xx) to milliseconds
wall_to_ms() {
    local wall_str="$1"
    if [ "$wall_str" = "N/A" ]; then echo "N/A"; return; fi
    # Format: 0:03.42 or 1:23.45
    echo "$wall_str" | awk -F: '{
        if (NF == 2) {
            ms = ($1 * 60 + $2) * 1000
        } else {
            ms = $1 * 1000
        }
        printf "%.0f", ms
    }' 2>/dev/null || echo "N/A"
}

echo "[INFO] ${GATE_NAME}: binary=$BINARY composition=$COMPOSITION frames=$FRAMES"
echo "[INFO] ${GATE_NAME}: nproc=$(nproc) RAM=$(free -h | awk '/^Mem:/{print $2}')"
echo "[INFO] ${GATE_NAME}: CpuBudget env: RENDER=${CHRONON3D_CPU_RENDER_THREADS:-default} DECODE=${CHRONON3D_CPU_DECODE_THREADS:-default} ENCODE=${CHRONON3D_CPU_ENCODE_THREADS:-default} CLASS=${CHRONON3D_CPU_MACHINE_CLASS:-default}"

# ── Run 1: Still render (pure render time, no encode) ──────────────
STILL_OUTPUT="${OUTPUT_DIR}/still_${TIMESTAMP}.png"
echo ""
echo "=== Run 1: Still render (frame 0, no encode) ==="
/usr/bin/time -v "$BINARY" still "$COMPOSITION" --frame 0 -o "$STILL_OUTPUT" 2>&1 | \
    tee "${OUTPUT_DIR}/still_${TIMESTAMP}.log" >/dev/null || true

STILL_WALL=$(extract_time_metric "${OUTPUT_DIR}/still_${TIMESTAMP}.log" "Elapsed.*wall")
STILL_WALL_MS=$(wall_to_ms "$STILL_WALL")
STILL_PEAK_RSS=$(extract_time_metric "${OUTPUT_DIR}/still_${TIMESTAMP}.log" "Maximum resident")
STILL_CPU_PCT=$(extract_time_metric "${OUTPUT_DIR}/still_${TIMESTAMP}.log" "Percent of CPU")

echo "  wall_ms=${STILL_WALL_MS}  peak_rss_kb=${STILL_PEAK_RSS}  cpu_pct=${STILL_CPU_PCT}"

# ── Run 2: Video render (pipe mode — interleaved render+encode) ────
VIDEO_OUTPUT="${OUTPUT_DIR}/video_pipe_${TIMESTAMP}.mp4"
echo ""
echo "=== Run 2: Video render (${FRAMES} frames, pipe mode) ==="
/usr/bin/time -v "$BINARY" video "$COMPOSITION" --start 0 --end "$FRAMES" --ffmpeg-mode pipe -o "$VIDEO_OUTPUT" 2>&1 | \
    tee "${OUTPUT_DIR}/video_pipe_${TIMESTAMP}.log" >/dev/null || true

PIPE_WALL=$(extract_time_metric "${OUTPUT_DIR}/video_pipe_${TIMESTAMP}.log" "Elapsed.*wall")
PIPE_WALL_MS=$(wall_to_ms "$PIPE_WALL")
PIPE_PEAK_RSS=$(extract_time_metric "${OUTPUT_DIR}/video_pipe_${TIMESTAMP}.log" "Maximum resident")
PIPE_CPU_PCT=$(extract_time_metric "${OUTPUT_DIR}/video_pipe_${TIMESTAMP}.log" "Percent of CPU")

# Try to extract render/encode split from spdlog [video] lines if present
# The CLI logs queue_wait_ms and encode write_blocked_ms but NOT a simple
# render_ms/encode_ms summary to stdout. The split is available via
# SQLite telemetry (if CHRONON3D_ENABLE_SQLITE_TELEMETRY=ON) or by
# analyzing per-frame timing. For now, wall_ms is the primary metric.
PIPE_QUEUE_WAIT=$(grep -oP 'queue wait.*?([\d.]+)\s*ms' "${OUTPUT_DIR}/video_pipe_${TIMESTAMP}.log" 2>/dev/null | grep -oP '[\d.]+' || echo "N/A")
PIPE_WRITE_BLOCKED=$(grep -oP 'write blocked.*?([\d.]+)\s*ms' "${OUTPUT_DIR}/video_pipe_${TIMESTAMP}.log" 2>/dev/null | grep -oP '[\d.]+' || echo "N/A")

echo "  wall_ms=${PIPE_WALL_MS}  queue_wait_ms=${PIPE_QUEUE_WAIT}  write_blocked_ms=${PIPE_WRITE_BLOCKED}"
echo "  peak_rss_kb=${PIPE_PEAK_RSS}  cpu_pct=${PIPE_CPU_PCT}"

# ── Run 3: Video render (chunked mode — separate render then encode) ──
VIDEO_CHUNKED_OUTPUT="${OUTPUT_DIR}/video_chunked_${TIMESTAMP}.mp4"
echo ""
echo "=== Run 3: Video render (${FRAMES} frames, chunked mode, 1 chunk = serial) ==="
# Use --chunks 1 so render is serial (apples-to-apples with still×frames).
# With --chunks 4, parallel render wall time < still×frames, making the
# encode heuristic produce a clamped-to-0 false negative.
/usr/bin/time -v "$BINARY" video "$COMPOSITION" --start 0 --end "$FRAMES" --ffmpeg-mode png --chunks 1 -o "$VIDEO_CHUNKED_OUTPUT" 2>&1 | \
    tee "${OUTPUT_DIR}/video_chunked_${TIMESTAMP}.log" >/dev/null || true

CHUNKED_WALL=$(extract_time_metric "${OUTPUT_DIR}/video_chunked_${TIMESTAMP}.log" "Elapsed.*wall")
CHUNKED_WALL_MS=$(wall_to_ms "$CHUNKED_WALL")
CHUNKED_PEAK_RSS=$(extract_time_metric "${OUTPUT_DIR}/video_chunked_${TIMESTAMP}.log" "Maximum resident")
CHUNKED_CPU_PCT=$(extract_time_metric "${OUTPUT_DIR}/video_chunked_${TIMESTAMP}.log" "Percent of CPU")

echo "  wall_ms=${CHUNKED_WALL_MS}  peak_rss_kb=${CHUNKED_PEAK_RSS}  cpu_pct=${CHUNKED_CPU_PCT}"

# ── Calculate derived metrics ──────────────────────────────────────
# frames/min from pipe mode wall time
if [ "$PIPE_WALL_MS" != "N/A" ] && [ "$PIPE_WALL_MS" -gt 0 ] 2>/dev/null; then
    FRAMES_PER_MIN=$(awk -v f="$FRAMES" -v w="$PIPE_WALL_MS" 'BEGIN{printf "%.1f", f / (w / 60000)}')
else
    FRAMES_PER_MIN="N/A"
fi

# Approximate render vs encode split from chunked mode:
# In chunked mode, render and encode are sequential. The wall time includes
# both. The still render gives us per-frame render cost.
# approx_render_total = still_wall_ms * frames
# approx_encode_total = chunked_wall_ms - approx_render_total
if [ "$STILL_WALL_MS" != "N/A" ] && [ "$CHUNKED_WALL_MS" != "N/A" ] 2>/dev/null; then
    APPROX_RENDER_TOTAL=$(awk -v s="$STILL_WALL_MS" -v f="$FRAMES" 'BEGIN{printf "%.0f", s * f}')
    APPROX_ENCODE_TOTAL=$(awk -v c="$CHUNKED_WALL_MS" -v r="$APPROX_RENDER_TOTAL" 'BEGIN{v=c-r; if(v<0) v=0; printf "%.0f", v}')
    ENCODE_RATIO=$(awk -v e="$APPROX_ENCODE_TOTAL" -v r="$APPROX_RENDER_TOTAL" 'BEGIN{t=e+r; if(t>0) printf "%.1f", e/t*100; else print "N/A"}')
else
    APPROX_RENDER_TOTAL="N/A"
    APPROX_ENCODE_TOTAL="N/A"
    ENCODE_RATIO="N/A"
fi

# ── Summary table ──────────────────────────────────────────────────
echo ""
echo "========================================"
echo "  CpuBudget Measurement Summary"
echo "========================================"
echo "  Composition:      $COMPOSITION"
echo "  Frames:           $FRAMES"
echo "  CPU cores:        $(nproc)"
echo "  RAM:              $(free -h | awk '/^Mem:/{print $2}')"
echo "  CpuBudget class:  ${CHRONON3D_CPU_MACHINE_CLASS:-desktop (default)}"
echo ""
echo "  Still (1 frame):  wall=${STILL_WALL_MS} ms  cpu=${STILL_CPU_PCT}%  rss=${STILL_PEAK_RSS} KB"
echo "  Pipe mode:        wall=${PIPE_WALL_MS} ms  cpu=${PIPE_CPU_PCT}%  rss=${PIPE_PEAK_RSS} KB"
echo "  Chunked mode:     wall=${CHUNKED_WALL_MS} ms  cpu=${CHUNKED_CPU_PCT}%  rss=${CHUNKED_PEAK_RSS} KB"
echo ""
echo "  Frames/min:       ${FRAMES_PER_MIN}"
echo "  Approx render:    ${APPROX_RENDER_TOTAL} ms (still×frames)"
echo "  Approx encode:    ${APPROX_ENCODE_TOTAL} ms (chunked - render)"
echo "  Encode ratio:     ${ENCODE_RATIO}% of (render+encode)"
echo "========================================"

# ── Decision rule ──────────────────────────────────────────────────
echo ""
if [ "$ENCODE_RATIO" != "N/A" ] 2>/dev/null; then
    THRESHOLD=25
    IS_SIGNIFICANT=$(awk -v r="$ENCODE_RATIO" -v t="$THRESHOLD" 'BEGIN{if(r>t) print 1; else print 0}')
    if [ "$IS_SIGNIFICANT" = "1" ]; then
        echo "[INFO] ${GATE_NAME}: encode_ratio=${ENCODE_RATIO}% > ${THRESHOLD}% — SIGNIFICANT"
        echo "[INFO] ${GATE_NAME}: RECOMMENDATION: extend CpuBudget with bounded pipeline"
        echo "[INFO] ${GATE_NAME}: Do NOT create WritePoolV2/EncoderPool/AsyncFrameWriter/BackgroundOutputManager"
    else
        echo "[INFO] ${GATE_NAME}: encode_ratio=${ENCODE_RATIO}% ≤ ${THRESHOLD}% — acceptable"
        echo "[INFO] ${GATE_NAME}: sequential render/encode cycle is adequate at current load"
    fi
else
    echo "[INFO] ${GATE_NAME}: encode_ratio=N/A — manual analysis required"
fi
echo "[INFO] ${GATE_NAME}: baseline saved to ${BASELINE_FILE}"

# Save baseline
{
    echo "# CpuBudget Measurement Baseline — ${TIMESTAMP}"
    echo "# Composition: $COMPOSITION  Frames: $FRAMES  Cores: $(nproc)"
    echo "# CpuBudget: class=${CHRONON3D_CPU_MACHINE_CLASS:-default} render=${CHRONON3D_CPU_RENDER_THREADS:-default} decode=${CHRONON3D_CPU_DECODE_THREADS:-default} encode=${CHRONON3D_CPU_ENCODE_THREADS:-default}"
    echo "still_wall_ms=${STILL_WALL_MS}"
    echo "still_cpu_pct=${STILL_CPU_PCT}"
    echo "still_peak_rss_kb=${STILL_PEAK_RSS}"
    echo "pipe_wall_ms=${PIPE_WALL_MS}"
    echo "pipe_cpu_pct=${PIPE_CPU_PCT}"
    echo "pipe_peak_rss_kb=${PIPE_PEAK_RSS}"
    echo "pipe_queue_wait_ms=${PIPE_QUEUE_WAIT}"
    echo "pipe_write_blocked_ms=${PIPE_WRITE_BLOCKED}"
    echo "chunked_wall_ms=${CHUNKED_WALL_MS}"
    echo "chunked_cpu_pct=${CHUNKED_CPU_PCT}"
    echo "chunked_peak_rss_kb=${CHUNKED_PEAK_RSS}"
    echo "frames_per_minute=${FRAMES_PER_MIN}"
    echo "approx_render_total_ms=${APPROX_RENDER_TOTAL}"
    echo "approx_encode_total_ms=${APPROX_ENCODE_TOTAL}"
    echo "encode_ratio_pct=${ENCODE_RATIO}"
    echo "decision_threshold_pct=25"
} > "$BASELINE_FILE"

echo "GATE_PASS: ${GATE_NAME}: measurement complete — see ${BASELINE_FILE}"
exit 0
