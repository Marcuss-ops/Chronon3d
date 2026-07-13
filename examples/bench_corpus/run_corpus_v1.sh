#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# run_corpus_v1.sh — F1.1 benchmark corpus runner.
#
# TICKET-BENCH-CORPUS-V1 — iterates the 12-scene corpus across the
# canonical measurement grid:
#   * presets:   cold | warm  (--warmup-renderer flag toggled)
#   * threads:   1, 2, 4, 8, 16  (CHRONON3D_THREADS env var)
#   * resolution: 1080p, 4K, portrait  (scene width × height per corpus_v1.json)
#
# fps dimension (30/60) is INTENTIONALLY NOT iterated:
#   * The frame-time metric (p50/p95/p99) is fps-independent because
#     rendering time scales with frame index, not playback rate.
#   * The only existing CLI command that exposes `--fps` is
#     `chronon3d_cli video <scene> --fps 30`. End-to-end fps-dependent
#     metrics (e.g. `costo_per_minuto_output`) are a separate concern
#     routed to a future per-scene video runner (forward-pointed to
#     TICKET-BENCH-MARKET-V1).
#   * corpus_v1.json declares `"fps": [30, 60]` as canonical metadata
#     for downstream consumers; the bench runner only hashes wall-clock
#     per frame, no fps arg passed.
#
# Cat-3 minimal-surface: ZERO new SDK API.  Uses ONLY:
#   * `chronon3d_cli bench`     — existing CLI (commands/bench/command_bench.cpp)
#   * `CHRONON3D_THREADS=N`     — existing env var (main.cpp reads it)
#   * `--warmup-renderer`       — existing RenderPipelineArgs flag
#
# Output: per-run JSON file at /tmp/bench_corpus/${SCENE}_${PRESET}_t${THREADS}.json
# Compatible with the F1.2 JSON schema (bench/benchmark_schema.json).
#
# Usage:
#   bash examples/bench_corpus/run_corpus_v1.sh                          # full sweep
#   bash examples/bench_corpus/run_corpus_v1.sh --only BenchB03_*       # single scene
#   bash examples/bench_corpus/run_corpus_v1.sh --threads 1,8           # thread subset
#   bash examples/bench_corpus/run_corpus_v1.sh --warmup-only           # skip cold preset
# ═══════════════════════════════════════════════════════════════════════════

set -uo pipefail

# ── Defaults ─────────────────────────────────────────────────────────────
THREADS_DEFAULT="1,2,4,8,16"
OUTPUT_DIR="/tmp/bench_corpus"
ONLY_PATTERN=""
WARMUP_ONLY="false"
SCENES_ALL=(
    "BenchB00_EmptyFrame"
    "BenchB01_StaticText1080p"
    "BenchB02_Typewriter200Glyphs"
    "BenchB03_CinematicGlow1080p"
    "BenchB04_Layers100"
    "BenchB05_Blur4K"
    "BenchB06_VideoOverlay1080p"
    "BenchB07_NestedPrecomps"
    "BenchB08_DirtyRectSmallMotion"
    "BenchB09_LongForm10Minutes"
    "BenchB10_RandomFrameAccess"
    "BenchB11_Portrait1080x1920"
)

# ── Arg parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --only)            ONLY_PATTERN="$2"; shift 2 ;;
        --threads)         THREADS_DEFAULT="$2"; shift 2 ;;
        --output-dir)      OUTPUT_DIR="$2"; shift 2 ;;
        --warmup-only)     WARMUP_ONLY="true"; shift ;;
        --help|-h)
            sed -n '2,30p' "$0"
            exit 0
            ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
done

# ── Locate the chronon3d_cli binary ──────────────────────────────────────
CLI_BIN="${CHRONON3D_CLI:-build/manual-test/chronon3d_cli}"
if [[ ! -x "$CLI_BIN" ]]; then
    echo "[ERROR] chronon3d_cli not found at $CLI_BIN — build the project first" >&2
    echo "         (cmake --build build/manual-test --target chronon3d_cli)" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# ── Per-scene helper ─────────────────────────────────────────────────────
run_scene() {
    local scene="$1"; local threads="$2"; local preset="$3"
    local width="$4"; local height="$5"; local duration="$6"

    local warm_flag=""
    if [[ "$preset" == "warm" ]]; then
        warm_flag="--warmup-renderer"
    fi

    local out_file="${OUTPUT_DIR}/${scene}_${preset}_t${threads}.json"
    local log_file="${OUTPUT_DIR}/${scene}_${preset}_t${threads}.log"

    echo "[BENCH]  ${scene}  preset=${preset}  threads=${threads}  ${width}x${height}"

    CHRONON3D_THREADS="$threads" \
    "$CLI_BIN" bench "$scene" \
        --frames "$duration" \
        --warmup 10 \
        $warm_flag \
        --json-file "$out_file" \
        --quiet \
        > "$log_file" 2>&1

    local ec=$?
    if [[ $ec -ne 0 ]]; then
        echo "[FAIL]   scene=${scene} preset=${preset} threads=${threads} exit=${ec}" >&2
        cat "$log_file" >&2
        return 1
    fi
    echo "[OK]     ${out_file}"
}

# ── Iterate the corpus grid ──────────────────────────────────────────────
total=0
ok=0
IFS=',' read -ra THREADS_ARR <<< "$THREADS_DEFAULT"

PRESETS_ALL=("cold" "warm")
if [[ "$WARMUP_ONLY" == "true" ]]; then
    PRESETS_ALL=("warm")
fi

for scene in "${SCENES_ALL[@]}"; do
    if [[ -n "$ONLY_PATTERN" && ! "$scene" == $ONLY_PATTERN ]]; then
        continue
    fi
    # Per-scene resolution + duration (canonical corpus grid).
    width=1920; height=1080; duration=90
    case "$scene" in
        BenchB05_Blur4K)              width=3840; height=2160 ;;
        BenchB09_LongForm10Minutes)   duration=18000 ;;
        BenchB10_RandomFrameAccess)   duration=360 ;;
        BenchB11_Portrait1080x1920)   width=1080; height=1920 ;;
        BenchB00_EmptyFrame)          duration=1 ;;
    esac

    for preset in "${PRESETS_ALL[@]}"; do
        # B09 LongForm: cold preset intentionally SKIPPED — the scene's
        # purpose is steady-state leak/allocator pressure detection; a cold
        # run of 18000 frames would burn unnecessary cold-cache budget
        # without providing the canonical long-form signal. Warm-only is
        # declared canonically in corpus_v1.json §scenes[8].presets.
        if [[ "$scene" == "BenchB09_LongForm10Minutes" && "$preset" == "cold" ]]; then
            echo "[SKIP]   scene=${scene} preset=cold (long-form: warm-only per corpus_v1.json)"
            continue
        fi

        for threads in "${THREADS_ARR[@]}"; do
            total=$((total + 1))
            if run_scene "$scene" "$threads" "$preset" \
                         "$width" "$height" "$duration"; then
                ok=$((ok + 1))
            fi
        done
    done
done

# ── Summary ──────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════════"
echo "Corpus v1 sweep complete"
echo "  Total runs :  ${total}"
echo "  Passed      :  ${ok}"
echo "  Failed      :  $((total - ok))"
echo "  Output dir  :  ${OUTPUT_DIR}"
echo "═══════════════════════════════════════════════════════════════════"

exit $((total - ok))
