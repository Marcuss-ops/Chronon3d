#!/usr/bin/env bash
set -euo pipefail

# Compare the GPU-native path with the current CPU-filter fallback.
#
# Usage:
#   tools/bench_gpu_clip.sh /path/to/input.mp4
#
# The input is intentionally external: benchmark clips may be private or
# supplied from Drive. The script never modifies the source file.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_DIR="$(cd "${ROOT_DIR}/.." && pwd)"
INPUT="${1:-${CHRONON_GPU_CLIP:-${WORKSPACE_DIR}/output/gpu-bench/drive-clip/input.mp4}}"
if [[ ! -f "${INPUT}" ]]; then
    echo "usage: $0 /path/to/input.mp4" >&2
    exit 2
fi

FFMPEG_BIN="${CHRONON_FFMPEG:-${FFMPEG_GPU_BIN:-$(command -v ffmpeg || true)}}"
FFPROBE_BIN="${CHRONON_FFPROBE:-$(command -v ffprobe || true)}"
FFMPEG_THREADS="${CHRONON_FFMPEG_THREADS:-1}"
FFMPEG_FILTER_THREADS="${CHRONON_FFMPEG_FILTER_THREADS:-1}"
FFMPEG_FILTER_COMPLEX_THREADS="${CHRONON_FFMPEG_FILTER_COMPLEX_THREADS:-1}"
[[ -x "${FFMPEG_BIN}" ]] || { echo "ffmpeg not found (set CHRONON_FFMPEG)" >&2; exit 2; }
[[ -x "${FFPROBE_BIN}" ]] || { echo "ffprobe not found (set CHRONON_FFPROBE)" >&2; exit 2; }

ASS_FILE="${ROOT_DIR}/bench/gpu_subtitle_benchmark.ass"
WATERMARK="${ROOT_DIR}/../RenderingGen/testdata/golden/apple.png"

FILTER_LIST="$(${FFMPEG_BIN} -hide_banner -filters 2>&1 || true)"
if ! grep -q 'overlay_cuda' <<<"${FILTER_LIST}"; then
    echo "overlay_cuda is not available in this FFmpeg build" >&2
    exit 3
fi

read -r WIDTH HEIGHT FPS DURATION < <(
    "${FFPROBE_BIN}" -v error -select_streams v:0 \
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

if ! "${FFMPEG_BIN}" -hide_banner -h filter=scale_cuda 2>&1 | grep -q 'format'; then
    echo "GPU FFmpeg must provide scale_cuda:format (required for YUVA420P alpha)" >&2
    exit 3
fi

SUBTITLE_LAYER="$(mktemp --suffix=.png chronon-gpu-subtitles.XXXXXX)"
SUBTITLE_CROP="${SUBTITLE_LAYER%.png}-crop.png"
RAW_WATERMARK="${SUBTITLE_LAYER%.png}-watermark.rgba"
RAW_SUBTITLE="${SUBTITLE_LAYER%.png}-subtitle.rgba"
trap 'rm -f "${SUBTITLE_LAYER}" "${SUBTITLE_CROP}" "${RAW_WATERMARK}" "${RAW_SUBTITLE}"' EXIT

# Rasterize only the subtitle layer on the CPU. The video never leaves CUDA;
# the resulting small alpha texture is uploaded once and composited by CUDA.
/usr/bin/ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "color=c=black@0.0:s=${WIDTH}x${HEIGHT}:r=24,format=rgba" \
    -vf "subtitles=filename='${ASS_FILE//:/\\:}':alpha=1" \
    -frames:v 1 -pix_fmt rgba "${SUBTITLE_LAYER}"

if command -v identify >/dev/null 2>&1 && command -v convert >/dev/null 2>&1; then
    SUBTITLE_GEOMETRY="$(identify -format '%@' "${SUBTITLE_LAYER}" 2>/dev/null || true)"
    if [[ -n "${SUBTITLE_GEOMETRY}" ]]; then
        convert "${SUBTITLE_LAYER}" -crop "${SUBTITLE_GEOMETRY}" +repage "${SUBTITLE_CROP}"
        read -r SUB_W SUB_H SUB_X SUB_Y < <(sed -E 's/x/ /; s/\+/ /g' <<<"${SUBTITLE_GEOMETRY}")
        SUBTITLE_LAYER="${SUBTITLE_CROP}"
    fi
else
    echo "identify and convert are required to crop the subtitle texture" >&2
    exit 3
fi

SUB_X="${SUB_X:-0}"
SUB_Y="${SUB_Y:-0}"

OUTPUT_DIR="${CHRONON_GPU_BENCH_OUTPUT_DIR:-${WORKSPACE_DIR}/output/gpu-bench/drive-clip}"
mkdir -p "${OUTPUT_DIR}"
GPU_NATIVE_OUTPUT="${OUTPUT_DIR}/bench_gpu_native.mp4"
GPU_ALPHA_OUTPUT="${OUTPUT_DIR}/bench_gpu_alpha.mp4"

echo "== GPU-native decode/encode (no CPU video filter) =="
/usr/bin/time -f 'wall=%e cpu=%P maxrss=%M' \
    "${FFMPEG_BIN}" -hide_banner -loglevel error -y -benchmark \
    -threads "${FFMPEG_THREADS}" -filter_threads "${FFMPEG_FILTER_THREADS}" -filter_complex_threads "${FFMPEG_FILTER_COMPLEX_THREADS}" \
    -hwaccel cuda -hwaccel_output_format cuda -extra_hw_frames 8 -i "${INPUT}" \
    -vf "scale_cuda=${WIDTH}:${HEIGHT}" -an \
    -c:v h264_nvenc -preset p1 -rc vbr -cq 23 -pix_fmt cuda \
    -f mp4 "${GPU_NATIVE_OUTPUT}"

echo
echo "== GPU alpha path (watermark + subtitle texture, no hwdownload) =="
CUDA_OVERLAY_BIN="${CHRONON_CUDA_OVERLAY_BIN:-${ROOT_DIR}/.tmp/chronon-builds/native-verify/src/backends/vulkan/chronon3d_cuda_nvdec_nvenc_overlay_bench}"
NATIVE_REQUIRED="${CHRONON_REQUIRE_NATIVE_CUDA_OVERLAY:-1}"
if [[ -x "${CUDA_OVERLAY_BIN}" ]]; then
    read -r WM_W WM_H < <(identify -format '%w %h\n' "${WATERMARK}")
    convert "${WATERMARK}" -background none -alpha on -colorspace sRGB -depth 8 rgba:"${RAW_WATERMARK}" || {
        echo "failed to rasterize watermark layer" >&2
        exit 3
    }
    convert "${SUBTITLE_LAYER}" -background none -alpha on -colorspace sRGB -depth 8 rgba:"${RAW_SUBTITLE}" || {
        echo "failed to rasterize subtitle layer" >&2
        exit 3
    }
    CUDA_LIB_PATH="${CHRONON_CUDA_LIBRARY_PATH:-/usr/local/lib/python3.10/dist-packages/nvidia/cu13/lib}"
    CUDA_PTX_CACHE="${CHRONON_CUDA_PTX_CACHE:-${OUTPUT_DIR}/chronon_cuda_overlay.ptx}"
    CUDA_DECODER_ARGS=()
    if [[ -n "${CHRONON_CUDA_DECODER:-}" ]]; then
        CUDA_DECODER_ARGS=(CHRONON_CUDA_DECODER="${CHRONON_CUDA_DECODER}")
        echo "native CUDA decoder=${CHRONON_CUDA_DECODER}"
    else
        echo "native CUDA decoder=automatic CUDA hw decoder"
    fi
    RAW_NVENC="${CHRONON_NVENC_RAW:-1}"
    if [[ "${RAW_NVENC}" == "1" ]]; then
        echo "native CUDA compositor=${CUDA_OVERLAY_BIN} (raw nvEncodeAPI)"
    else
        echo "native CUDA compositor=${CUDA_OVERLAY_BIN} (libavcodec NVENC)"
    fi
    CHRONON_CUDA_PTX_CACHE="${CUDA_PTX_CACHE}" \
    LD_LIBRARY_PATH="${CUDA_LIB_PATH}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
    env "${CUDA_DECODER_ARGS[@]}" CHRONON_NVENC_RAW="${RAW_NVENC}" /usr/bin/time -f 'wall=%e cpu=%P maxrss=%M' \
        "${CUDA_OVERLAY_BIN}" "${INPUT}" "${GPU_ALPHA_OUTPUT}" \
        "${RAW_WATERMARK}" "${WM_W}" "${WM_H}" 40 40 \
        "${RAW_SUBTITLE}" "${SUB_W}" "${SUB_H}" "${SUB_X}" "${SUB_Y}"
else
    if [[ "${NATIVE_REQUIRED}" == "1" ]]; then
        echo "native CUDA compositor not found: build chronon3d_cuda_nvdec_nvenc_overlay_bench or set CHRONON_CUDA_OVERLAY_BIN" >&2
        exit 3
    fi
    echo "native CUDA compositor unavailable; using explicitly labelled FFmpeg CUDA path" >&2
    /usr/bin/time -f 'wall=%e cpu=%P maxrss=%M' \
        "${FFMPEG_BIN}" -hide_banner -loglevel error -y -benchmark \
        -threads "${FFMPEG_THREADS}" -filter_threads "${FFMPEG_FILTER_THREADS}" -filter_complex_threads "${FFMPEG_FILTER_COMPLEX_THREADS}" \
        -hwaccel cuda -hwaccel_output_format cuda -extra_hw_frames 8 -i "${INPUT}" \
        -framerate 1 -loop 1 -i "${WATERMARK}" \
        -framerate 1 -loop 1 -i "${SUBTITLE_LAYER}" \
        -filter_complex \
        "[0:v]scale_cuda=${WIDTH}:${HEIGHT}:format=yuv420p[base];\
[1:v]format=rgba,colorchannelmixer=aa=0.75,format=yuva420p,hwupload_cuda[wm];\
[base][wm]overlay_cuda=x=40:y=40[wmv];\
[2:v]format=yuva420p,hwupload_cuda[subs];\
[wmv][subs]overlay_cuda=x=${SUB_X}:y=${SUB_Y}:shortest=0,\
scale_cuda=${WIDTH}:${HEIGHT}:format=yuv420p:passthrough=0[v]" \
        -map '[v]' -an -c:v h264_nvenc -preset p1 -rc vbr -cq 23 \
        -pix_fmt cuda -t "${DURATION}" -f mp4 "${GPU_ALPHA_OUTPUT}"
fi

read -r OUT_W OUT_H < <(
    "${FFPROBE_BIN}" -v error -select_streams v:0 \
        -show_entries stream=width,height -of default=nw=1:nk=1 "${GPU_ALPHA_OUTPUT}" \
        | awk 'NR == 1 { w=$0 } NR == 2 { h=$0 } END { print w, h }'
)
if [[ "${OUT_W}x${OUT_H}" != "${WIDTH}x${HEIGHT}" ]]; then
    echo "GPU alpha output dimension mismatch: got ${OUT_W}x${OUT_H}, expected ${WIDTH}x${HEIGHT}" >&2
    exit 4
fi
echo "GPU alpha output=${GPU_ALPHA_OUTPUT} (${OUT_W}x${OUT_H})"
