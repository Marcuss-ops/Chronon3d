#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# bench/benchmark_pipeline_stages.sh — Multi-stage pipeline benchmarks
#
# Measures Chronon3D performance at 6 independent pipeline stages WITHOUT
# PNG intermediates. Each stage isolates a specific part of the pipeline:
#
#   1. render-null        — GPU/CPU render pass only (no output, no encode)
#   2. text-compositor    — Text composition timing (atlas + shaping + raster)
#   3. nv12-compositor    — NV12/P010 composition (RGB→YUV direct)
#   4. nvenc-native       — NVENC encoding only (input frames → encoded)
#   5. render-to-nvenc    — Render directly to NVENC (no intermediate copy)
#   6. full-video-export  — Complete pipeline (render + compose + encode + mux)
#
# All stages produce JSON telemetry with wall-clock breakdowns.
# NO PNG intermediates are used in any stage.
#
# Usage:
#   bash bench/benchmark_pipeline_stages.sh --stage render-null --frames 60
#   bash bench/benchmark_pipeline_stages.sh --stage all --frames 60
#   bash bench/benchmark_pipeline_stages.sh --stage nvenc-native --frames 120 --repetitions 3
#
# Environment:
#   CHRONON3D_CLI          Path to chronon3d_cli binary (default: build/.../chronon3d_cli)
#   CHRONON3D_BENCH_OUTPUT Output directory (default: /tmp/bench_stages)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Defaults ─────────────────────────────────────────────────────────────
STAGE="${CHRONON3D_BENCH_STAGE:-}"
FRAMES="${CHRONON3D_BENCH_FRAMES:-60}"
WARMUP="${CHRONON3D_BENCH_WARMUP:-10}"
REPETITIONS="${CHRONON3D_BENCH_REPETITIONS:-3}"
SCENE="${CHRONON3D_BENCH_SCENE:-TextPlaceAnimatedCenter}"
WIDTH="${CHRONON3D_BENCH_WIDTH:-1920}"
HEIGHT="${CHRONON3D_BENCH_HEIGHT:-1080}"
OUTPUT_DIR="${CHRONON3D_BENCH_OUTPUT:-/tmp/bench_stages}"
ASSETS_ROOT="${CHRONON3D_BENCH_ASSETS:-}"
FPS_NUM="${CHRONON3D_BENCH_FPS_NUM:-30}"
FPS_DEN="${CHRONON3D_BENCH_FPS_DEN:-1}"
QUIET="false"

CLI_BIN="${CHRONON3D_CLI:-}"
if [[ -z "$CLI_BIN" ]]; then
    # Try common build paths
    for candidate in \
        "$REPO_ROOT/build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli" \
        "$REPO_ROOT/build/fast/apps/chronon3d_cli/chronon3d_cli" \
        "$REPO_ROOT/build/apps/chronon3d_cli/chronon3d_cli"; do
        if [[ -x "$candidate" ]]; then
            CLI_BIN="$candidate"
            break
        fi
    done
fi

# ── Arg parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --stage)        STAGE="$2"; shift 2 ;;
        --frames)       FRAMES="$2"; shift 2 ;;
        --warmup)       WARMUP="$2"; shift 2 ;;
        --repetitions)  REPETITIONS="$2"; shift 2 ;;
        --scene)        SCENE="$2"; shift 2 ;;
        --width)        WIDTH="$2"; shift 2 ;;
        --height)       HEIGHT="$2"; shift 2 ;;
        --output-dir)   OUTPUT_DIR="$2"; shift 2 ;;
        --assets-root)  ASSETS_ROOT="$2"; shift 2 ;;
        --fps)          FPS_NUM="$2"; FPS_DEN="${3:-1}"; shift 2 ;;
        --quiet|-q)     QUIET="true"; shift ;;
        --help|-h)
            sed -n '2,35p' "$0"
            exit 0
            ;;
        *) echo "[ERROR] Unknown arg: $1" >&2; exit 2 ;;
    esac
done

# ── Validation ───────────────────────────────────────────────────────────
if [[ -z "$STAGE" ]]; then
    echo "[ERROR] --stage is required (render-null|text-compositor|nv12-compositor|nvenc-native|render-to-nvenc|full-video-export|all)" >&2
    exit 2
fi

if [[ ! -x "$CLI_BIN" ]]; then
    echo "[ERROR] chronon3d_cli not found at: $CLI_BIN" >&2
    echo "        Set CHRONON3D_CLI or build the project first." >&2
    exit 2
fi

mkdir -p "$OUTPUT_DIR"
COMMIT="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"

# ── Helpers ──────────────────────────────────────────────────────────────
_info() { echo "[INFO] benchmark_pipeline_stages: $*" >&2; }
_warn() { echo "[WARN] benchmark_pipeline_stages: $*" >&2; }

build_common_args() {
    local args=()
    args+=(render "$SCENE")
    args+=(--frames "$FRAMES")
    args+=(--warmup "$WARMUP")
    args+=(--width "$WIDTH")
    args+=(--height "$HEIGHT")
    args+=(--fps "$FPS_NUM" "$FPS_DEN")
    args+=(--report)
    if [[ -n "$ASSETS_ROOT" ]]; then
        args+=(--assets-root "$ASSETS_ROOT")
    fi
    echo "${args[@]}"
}

run_stage() {
    local stage_name="$1"
    local extra_args=("${@:2}")
    local out_json="$OUTPUT_DIR/${stage_name}_${COMMIT}_${TIMESTAMP}.json"
    local out_log="$OUTPUT_DIR/${stage_name}_${COMMIT}_${TIMESTAMP}.log"
    local total_ms=0
    local p50_ms=0
    local p95_ms=0
    local peak_rss=0

    _info "Stage: $stage_name (frames=$FRAMES, warmup=$WARMUP, scene=$SCENE)"
    _info "  Output: $out_json"

    local stage_start
    stage_start=$(date +%s%N)

    local common_args
    common_args=$(build_common_args)

    # Run with repetitions
    for ((r=1; r<=REPETITIONS; r++)); do
        _info "  Repetition $r/$REPETITIONS"
        if ! "$CLI_BIN" $common_args "${extra_args[@]}" \
            -o "$OUTPUT_DIR/${stage_name}_frame_${r}_####.null" \
            > "$out_log" 2>&1; then
            _warn "  Repetition $r failed — see $out_log"
        fi
    done

    local stage_end
    stage_end=$(date +%s%N)
    local elapsed_ns=$(( stage_end - stage_start ))
    total_ms=$(( elapsed_ns / 1000000 ))

    # Extract metrics from CLI report if available
    if [[ -f "$out_log" ]]; then
        # Parse wall time breakdown from report
        p50_ms=$(grep -oP 'frame_p50_ms["\s:]+\K[0-9.]+' "$out_log" 2>/dev/null || echo "0")
        p95_ms=$(grep -oP 'frame_p95_ms["\s:]+\K[0-9.]+' "$out_log" 2>/dev/null || echo "0")
        peak_rss=$(grep -oP 'peak_rss_mb["\s:]+\K[0-9.]+' "$out_log" 2>/dev/null || echo "0")
    fi

    # Write stage summary
    cat > "$out_json" <<EOF
{
  "stage": "$stage_name",
  "scene": "$SCENE",
  "commit": "$COMMIT",
  "timestamp": "$TIMESTAMP",
  "frames": $FRAMES,
  "warmup": $WARMUP,
  "repetitions": $REPETITIONS,
  "resolution": "${WIDTH}x${HEIGHT}",
  "fps": "$FPS_NUM/$FPS_DEN",
  "total_wall_ms": $total_ms,
  "frame_p50_ms": $p50_ms,
  "frame_p95_ms": $p95_ms,
  "peak_rss_mb": $peak_rss
}
EOF

    _info "  Stage $stage_name complete: ${total_ms}ms total"
    if [[ "$QUIET" != "true" ]]; then
        echo "  $stage_name: ${total_ms}ms total, p50=${p50_ms}ms, p95=${p95_ms}ms, peak_rss=${peak_rss}MB"
    fi
}

# ── Stage 1: render-null ─────────────────────────────────────────────────
# Renders frames but discards output. Measures pure render pass time.
stage_render_null() {
    run_stage "render-null" \
        --sink null-render
}

# ── Stage 2: text-compositor ────────────────────────────────────────────
# Renders text composition only. Isolates atlas generation, shaping, raster.
stage_text_compositor() {
    run_stage "text-compositor" \
        --sink null-render \
        --text-only
}

# ── Stage 3: nv12-compositor ────────────────────────────────────────────
# Measures NV12/P010 composition (RGB→YUV direct conversion).
stage_nv12_compositor() {
    run_stage "nv12-compositor" \
        --sink raw \
        --output-format nv12
}

# ── Stage 4: nvenc-native ───────────────────────────────────────────────
# Measures NVENC encoding only. Requires NVIDIA GPU.
stage_nvenc_native() {
    run_stage "nvenc-native" \
        --sink ffmpeg \
        --encoder h264_nvenc \
        --output "$OUTPUT_DIR/nvenc_native_${COMMIT}_${TIMESTAMP}.mp4"
}

# ── Stage 5: render-to-nvenc ────────────────────────────────────────────
# Render directly to NVENC without intermediate copies.
stage_render_to_nvenc() {
    run_stage "render-to-nvenc" \
        --sink ffmpeg \
        --encoder h264_nvenc \
        --native-surface \
        --output "$OUTPUT_DIR/render_to_nvenc_${COMMIT}_${TIMESTAMP}.mp4"
}

# ── Stage 6: full-video-export ──────────────────────────────────────────
# Complete pipeline: render + compose + encode + mux to MP4.
stage_full_video_export() {
    run_stage "full-video-export" \
        --sink ffmpeg \
        --output "$OUTPUT_DIR/full_video_export_${COMMIT}_${TIMESTAMP}.mp4"
}

# ── Main dispatch ────────────────────────────────────────────────────────
_info "Configuration:"
_info "  CLI:        $CLI_BIN"
_info "  Stage:      $STAGE"
_info "  Scene:      $SCENE"
_info "  Frames:     $FRAMES (warmup: $WARMUP)"
_info "  Resolution: ${WIDTH}x${HEIGHT}"
_info "  FPS:        $FPS_NUM/$FPS_DEN"
_info "  Output:     $OUTPUT_DIR"
_info "  Commit:     $COMMIT"

case "$STAGE" in
    render-null)       stage_render_null ;;
    text-compositor)   stage_text_compositor ;;
    nv12-compositor)   stage_nv12_compositor ;;
    nvenc-native)      stage_nvenc_native ;;
    render-to-nvenc)   stage_render_to_nvenc ;;
    full-video-export) stage_full_video_export ;;
    all)
        _info "Running all 6 pipeline stages..."
        stage_render_null
        stage_text_compositor
        stage_nv12_compositor
        stage_nvenc_native
        stage_render_to_nvenc
        stage_full_video_export
        _info "All stages complete. Reports in: $OUTPUT_DIR"
        ;;
    *)
        echo "[ERROR] Unknown stage: $STAGE" >&2
        echo "        Valid stages: render-null|text-compositor|nv12-compositor|nvenc-native|render-to-nvenc|full-video-export|all" >&2
        exit 2
        ;;
esac

_info "Benchmark complete. Reports saved to: $OUTPUT_DIR"
