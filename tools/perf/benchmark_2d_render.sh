#!/usr/bin/env bash
# Measure representative CPU 2D renders with hyperfine.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT_DIR"

CLI_BIN="${CHRONON3D_CLI:-}"
OUTPUT_DIR="${CHRONON3D_BENCH_OUTPUT_DIR:-.tmp/benchmarks/2d}"
RUNS="${CHRONON3D_BENCH_RUNS:-5}"
WARMUP="${CHRONON3D_BENCH_WARMUP:-1}"
ONLY=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --only) ONLY="$2"; shift 2 ;;
        --runs) RUNS="$2"; shift 2 ;;
        --warmup) WARMUP="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --cli) CLI_BIN="$2"; shift 2 ;;
        -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ -z "$CLI_BIN" ]]; then
    for candidate in \
        "$ROOT_DIR/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli" \
        "$ROOT_DIR/build/manual-test/chronon3d_cli" \
        "$ROOT_DIR/build/chronon3d_cli"; do
        if [[ -x "$candidate" ]]; then CLI_BIN="$candidate"; break; fi
    done
fi

command -v hyperfine >/dev/null 2>&1 || { echo "BENCHMARK_2D_FAIL: hyperfine not found" >&2; exit 1; }
[[ -x "$CLI_BIN" ]] || { echo "BENCHMARK_2D_FAIL: CLI not found" >&2; exit 1; }
mkdir -p "$OUTPUT_DIR"

SCENES=(
    BenchB01_StaticText1080p BenchB02_Typewriter200Glyphs
    ImportantPhrasesStackFast CertMultilingual ImportantStoryImage
)

for scene in "${SCENES[@]}"; do
    [[ -n "$ONLY" && "$scene" != "$ONLY" ]] && continue
    safe_name="${scene//[^A-Za-z0-9_.-]/_}"
    output="$OUTPUT_DIR/${safe_name}.png"
    json="$OUTPUT_DIR/${safe_name}.hyperfine.json"
    echo "[BENCH] $scene"
    hyperfine --shell=bash --runs "$RUNS" --warmup "$WARMUP" \
        --export-json "$json" --command-name "$scene" \
        "\"$CLI_BIN\" render $scene --frame 0 --output \"$output\" --profile preview --log-level error >/dev/null"
    [[ -s "$output" ]] || { echo "BENCHMARK_2D_FAIL: no output for $scene" >&2; exit 1; }
done

echo "BENCHMARK_2D_PASS: results in $OUTPUT_DIR"
