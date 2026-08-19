#!/usr/bin/env bash
set -euo pipefail

# Compare the GPU-native path with the current CPU-filter fallback.
#
# Usage:
#   tools/bench_gpu_clip.sh /path/to/input.mp4
#
# The input is intentionally external: benchmark clips may be private or
# supplied from Drive. The script never modifies the source file.

INPUT="${1:-}"
if [[ -z "${INPUT}" || ! -f "${INPUT}" ]]; then
    echo "usage: $0 /path/to/input.mp4" >&2
    exit 2
fi

command -v ffmpeg >/dev/null || { echo "ffmpeg not found" >&2; exit 2; }
command -v ffprobe >/dev/null || { echo "ffprobe not found" >&2; exit 2; }

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASS_FILE="${ROOT_DIR}/bench/gpu_subtitle_benchmark.ass"
WATERMARK="${ROOT_DIR}/../RenderingGen/testdata/golden/apple.png"

FILTER_LIST="$(ffmpeg -hide_banner -filters 2>&1 || true)"
if ! grep -q 'overlay_cuda' <<<"${FILTER_LIST}"; then
    echo "overlay_cuda is not available in this FFmpeg build" >&2
    exit 3
fi

read -r WIDTH HEIGHT FPS DURATION < <(
    ffprobe -v error -select_streams v:0 \
        -show_entries stream=width,height,r_frame_rate:format=duration \
        -of default=nw=1:nk=1 "${INPUT}" | awk '
        NR == 1 { w=$0 }
        NR == 2 { h=$0 }
        NR == 3 { fps=$0 }
        NR == 4 { d=$0 }
        END { print w, h, fps, d }'
)

echo "input=${INPUT}"
echo "video=${WIDTH}x${HEIGHT} fps=${FPS} duration=${DURATION}s"
echo

echo "== GPU-native decode/encode (no CPU video filter) =="
/usr/bin/time -f 'wall=%e cpu=%P maxrss=%M' \
    ffmpeg -hide_banner -loglevel error -y -benchmark \
    -hwaccel cuda -hwaccel_output_format cuda -i "${INPUT}" \
    -vf "scale_cuda=${WIDTH}:${HEIGHT}" -an \
    -c:v h264_nvenc -preset p1 -rc vbr -cq 23 -pix_fmt cuda \
    -f null -

echo
echo "== Current correctness fallback (watermark alpha + libass) =="
/usr/bin/time -f 'wall=%e cpu=%P maxrss=%M' \
    ffmpeg -hide_banner -loglevel error -y -benchmark \
    -hwaccel cuda -hwaccel_output_format cuda -i "${INPUT}" \
    -loop 1 -i "${WATERMARK}" \
    -filter_complex \
    "[0:v]hwdownload,format=nv12[base];\
[1:v]format=rgba,colorchannelmixer=aa=0.75[wm];\
[base][wm]overlay=x=40:y=40:format=auto[v1];\
[v1]subtitles=filename='${ASS_FILE//:/\\:}'[v]" \
    -map '[v]' -an -c:v h264_nvenc -preset p1 -rc vbr -cq 23 \
    -pix_fmt yuv420p -vsync 0 -t "${DURATION}" -f null -
