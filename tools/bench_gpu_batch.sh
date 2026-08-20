#!/usr/bin/env bash
set -euo pipefail

# Run the native CUDA watermark/subtitle benchmark over several clips while
# keeping the GPU fed.  Each clip gets an isolated output directory so the
# benchmark remains safe under concurrent workers.
#
# Usage:
#   tools/bench_gpu_batch.sh -w 5 -o /tmp/chronon-batch clip1.mp4 clip2.mp4 ...

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKERS=5
OUTPUT_DIR="${ROOT_DIR}/../output/gpu-bench/batch"

while getopts ":w:o:" opt; do
    case "$opt" in
        w) WORKERS="$OPTARG" ;;
        o) OUTPUT_DIR="$OPTARG" ;;
        *) echo "usage: $0 [-w workers] [-o output-dir] clip..." >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))

[[ "$WORKERS" =~ ^[1-9][0-9]*$ ]] || { echo "workers must be positive" >&2; exit 2; }
(( $# > 0 )) || { echo "at least one clip is required" >&2; exit 2; }
mkdir -p "$OUTPUT_DIR"

export CHRONON_BATCH_ROOT="$ROOT_DIR"
export CHRONON_BATCH_OUTPUT="$OUTPUT_DIR"

start_ms=$(date +%s%3N)
printf '%s\n' "$@" | nl -v1 -w1 -s $'\t' |
    xargs -P "$WORKERS" -n2 bash -c '
        index="$0"
        clip="$1"
        output="$CHRONON_BATCH_OUTPUT/clip-$index"
        mkdir -p "$output"
        if CHRONON_GPU_BENCH_OUTPUT_DIR="$output" \
            "$CHRONON_BATCH_ROOT/tools/bench_gpu_clip.sh" "$clip" \
            >"$output/run.log" 2>&1; then
            printf "PASS clip=%s output=%s\\n" "$index" "$output"
        else
            printf "FAIL clip=%s output=%s\\n" "$index" "$output" >&2
            exit 1
        fi
    '

end_ms=$(date +%s%3N)
printf 'BATCH_PASS clips=%d workers=%d wall_ms=%d output=%s\n' \
    "$#" "$WORKERS" "$((end_ms - start_ms))" "$OUTPUT_DIR"
