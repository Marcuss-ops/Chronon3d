#!/usr/bin/env bash
# check_video_completeness.sh — canonical FAIL-LOUD gate for spec §4+§6
# Video Completeness Matrix.
#
# Per AGENTS.md §honest-limitation + §INFO-level diagnostic style + Cat-3
# zero new public SDK API surface:
#
#   Step 0: precondition via canonical tools/check_ffmpeg_required.sh
#           (FAIL-LOUD with em-dash canonical `apt install ffmpeg` hint
#            when ffmpeg/ffprobe are absent / non-functional)
#   Step 1: ffprobe MP4  →  $PROBE_JSON  (canonical 1920x1080@30fps 60-frame
#                                         user-spec ChrononGlowFinalAE export)
#   Step 2: Python-side assertion on probe.json (7 fields per user spec
#           verbatim: width=1920 / height=1080 / fps≈30.0 / nb_read_frames=60
#           / duration≈2.0±0.05 / codec ∈ {h264,hevc,av1} / pix_fmt ∈ {yuv420p,
#           yuv444p,yuv420p10le,yuv444p10le})
#   Step 3: ffmpeg decode MP4 → decoded_frames/frame_NNNNNN.png  + assert
#           decoded PNG count == 60 (canonical 60-frame export verification)
#   Step 4: PASS canonical  + addizionale [INFO] ${GATE_NAME}: …  line
#           per AGENTS.md `## Regole di lint documentale` §INFO-level
#           diagnostic style rule (≤200 chars, grep-discoverable)
#
# Exit codes: 0 = GATE_PASS, 1 = GATE_FAIL (precondition / assertion),
#             2 = GATE_FAIL_INTERNAL (python3 / ffprobe / ffmpeg  missing).
#
# Cross-link: tools/check_ffmpeg_required.sh (the canonical ffmpeg/ffprobe
# FAIL-LOUD gate wired in commit `49205d27` per the TICKET-FFMPEG-REQUIRED-
# FAIL-LOUD closure lineage); tests/text/test_pipeline_parity_real.cpp
# Fase 6 §5 commit `e689820b` (the test that produces the input MP4 at
# $CHRONON3D_TEST_VIDEO_OUTPUT_DIR/chronon_glow_final.mp4).
set -euo pipefail

GATE_NAME=check_video_completeness
readonly GATE_NAME

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
readonly SCRIPT_DIR
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." &>/dev/null && pwd)"
readonly REPO_ROOT

# ── Step 0: precondition via canonical ffmpeg/ffprobe FAIL-LOUD gate ────────────
bash "${SCRIPT_DIR}/check_ffmpeg_required.sh"

# ── Resolve probe input MP4 (env-override-able for cross-host CI) ──────────────
if [[ -n "${CHRONON3D_VIDEO_PROBE_INPUT:-}" ]]; then
    PROBE_INPUT="${CHRONON3D_VIDEO_PROBE_INPUT}"
else
    PROBE_INPUT="${REPO_ROOT}/output/text_video_acceptance/chronon_glow_final.mp4"
fi
readonly PROBE_INPUT
if [[ ! -f "${PROBE_INPUT}" ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: probe input MP4 not found at ${PROBE_INPUT}" >&2
    echo "  Hint: run tests/text/test_pipeline_parity_real.cpp::Fase 6 first" >&2
    echo "  OR set CHRONON3D_VIDEO_PROBE_INPUT env var to an existing MP4" >&2
    exit 1
fi

PROBE_DIR="$(dirname "${PROBE_INPUT}")"
PROBE_JSON="${PROBE_DIR}/probe.json"
DECODED_DIR="${PROBE_DIR}/decoded_frames"
readonly PROBE_JSON DECODED_DIR

# ── Step 1: ffprobe MP4 → probe.json (fail-loud on ffprobe failure) ─────────────
ffprobe -v error -count_frames -select_streams v:0 \
    -show_entries \
        stream=codec_name,width,height,pix_fmt,r_frame_rate,avg_frame_rate,nb_read_frames,duration \
    -of json "${PROBE_INPUT}" > "${PROBE_JSON}" || {
    echo "GATE_FAIL: ${GATE_NAME}: ffprobe failed on ${PROBE_INPUT}" >&2
    exit 1
}

# ── Step 2: Python-side assertion on probe.json (7 fields per user spec) ───────
python3 - "${PROBE_JSON}" <<'PY' || exit 1
import json, sys
data_path = sys.argv[1]
try:
    data = json.load(open(data_path))
except Exception as e:
    print(f'GATE_FAIL: check_video_completeness: cannot parse {data_path}: {e}', file=sys.stderr)
    sys.exit(1)
streams = data.get('streams', [])
if not streams:
    print(f'GATE_FAIL: check_video_completeness: no streams in {data_path}', file=sys.stderr)
    sys.exit(1)
s = streams[0]
def fail(msg):
    print(f'GATE_FAIL: check_video_completeness: {msg}', file=sys.stderr)
    sys.exit(1)
# Width + height
if s.get('width') != 1920:
    fail(f'width != 1920 (got {s.get("width")})')
if s.get('height') != 1080:
    fail(f'height != 1080 (got {s.get("height")})')
# fps ≈ 30.0 (fractional: "30/1" or "30000/1001")
fps_str = s.get('r_frame_rate', '0/0')
n, d = (fps_str.split('/', 1) + ['1'])[:2]
try:
    fps = (float(n) / float(d)) if float(d) else 0.0
except Exception:
    fail(f'fps unparseable (got {fps_str!r})')
if not (29.95 <= fps <= 30.05):
    fail(f'fps ≈ 30 expected (got {fps_str} = {fps:.4f})')
# nb_read_frames == 60
if int(s.get('nb_read_frames', 0)) != 60:
    fail(f'nb_read_frames != 60 (got {s.get("nb_read_frames")})')
# duration ≈ 2.0 ± 0.05 (= 60 frames @ 30 fps)
try:
    dur = float(s.get('duration', 0))
except Exception:
    fail(f'duration unparseable (got {s.get("duration")!r})')
if not (1.95 <= dur <= 2.05):
    fail(f'duration ≈ 2.0 ± 0.05 expected (got {dur:.4f})')
# codec ∈ {h264,hevc,av1}
if s.get('codec_name') not in {'h264', 'hevc', 'av1'}:
    fail(f'codec_name ∉ {{h264,hevc,av1}} (got {s.get("codec_name")})')
# pix_fmt ∈ {yuv420p, yuv444p, yuv420p10le, yuv444p10le}
if s.get('pix_fmt') not in {'yuv420p', 'yuv444p', 'yuv420p10le', 'yuv444p10le'}:
    fail(f'pix_fmt ∉ {{yuv420p,yuv444p,yuv420p10le,yuv444p10le}} (got {s.get("pix_fmt")})')
PY

# ── Step 3: ffmpeg decode MP4 → decoded_frames/frame_NNNNNN.png ────────────────
rm -rf "${DECODED_DIR}"
mkdir -p "${DECODED_DIR}"
ffmpeg -v error -i "${PROBE_INPUT}" -vsync 0 "${DECODED_DIR}/frame_%06d.png" || {
    echo "GATE_FAIL: ${GATE_NAME}: ffmpeg decode failed on ${PROBE_INPUT}" >&2
    exit 1
}
DECODED_COUNT="$(find "${DECODED_DIR}" -type f -name 'frame_*.png' | wc -l | tr -d ' ')"
if [[ "${DECODED_COUNT}" != "60" ]]; then
    echo "GATE_FAIL: ${GATE_NAME}: decoded PNG count != 60 (got ${DECODED_COUNT})" >&2
    exit 1
fi

# ── Step 4: PASS canonical + addizionale [INFO] line per AGENTS.md style ───────
echo "GATE_PASS: ${GATE_NAME}: probe.json verified + 60/60 decoded frames match"

FFMPEG_VER="$(ffmpeg -version 2>&1 | head -n1 | awk '{print $3; exit}')"
FFPROBE_VER="$(ffprobe -version 2>&1 | head -n1 | awk '{print $3; exit}')"
INFO_LINE="[INFO] ${GATE_NAME}: ffmpeg/ffprobe present at ${FFMPEG_VER}/${FFPROBE_VER} + 60 PNGs decoded matching probe.json 1920x1080@~30fps dur~2.0s"
if [[ ${#INFO_LINE} -gt 200 ]]; then
    INFO_LINE="${INFO_LINE:0:197}..."
fi
echo "${INFO_LINE}"

exit 0
