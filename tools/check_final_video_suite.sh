#!/usr/bin/env bash
# Validate an already-rendered FINAL-01..FINAL-10 artifact directory.
set -euo pipefail
GATE_NAME=check_final_video_suite
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
MANIFEST="${CHRONON3D_FINAL_SUITE_MANIFEST:-${REPO_ROOT}/examples/final_video_suite.json}"
OUTPUT_DIR="${CHRONON3D_FINAL_SUITE_OUTPUT:-${REPO_ROOT}/output/final_video_suite}"
command -v python3 >/dev/null 2>&1 || { echo "GATE_FAIL: ${GATE_NAME}: python3 not found" >&2; exit 1; }
command -v ffprobe >/dev/null 2>&1 || { echo "GATE_FAIL: ${GATE_NAME}: ffprobe not found" >&2; exit 1; }
command -v ffmpeg >/dev/null 2>&1 || { echo "GATE_FAIL: ${GATE_NAME}: ffmpeg not found" >&2; exit 1; }

python3 - "${MANIFEST}" "${OUTPUT_DIR}" <<'PY'
import json, pathlib, subprocess, sys
manifest = json.load(open(sys.argv[1]))
root = pathlib.Path(sys.argv[2])
failures = []
for c in manifest["cases"]:
    stem = f'{c["id"]}-{c["name"]}'
    mp4, sidecar = root / f'{stem}.mp4', root / f'{stem}.telemetry.json'
    if not mp4.is_file(): failures.append(f'{c["id"]}: missing MP4'); continue
    if not sidecar.is_file(): failures.append(f'{c["id"]}: missing telemetry sidecar'); continue
    try:
        s = json.loads(subprocess.check_output([
            'ffprobe','-v','error','-count_frames','-select_streams','v:0',
            '-show_entries','stream=width,height,r_frame_rate,avg_frame_rate,nb_read_frames,codec_name,pix_fmt',
            '-of','json',str(mp4)], text=True))['streams'][0]
        n,d = (s.get('avg_frame_rate') or s['r_frame_rate']).split('/',1)
        fps = float(n)/float(d)
        if (s.get('width'), s.get('height')) != (manifest['width'], manifest['height']):
            failures.append(f'{c["id"]}: resolution {s.get("width")}x{s.get("height")}')
        if not (manifest['fps'] - .05 <= fps <= manifest['fps'] + .05):
            failures.append(f'{c["id"]}: fps {fps}')
        if int(s.get('nb_read_frames', 0)) != c['frames']:
            failures.append(f'{c["id"]}: decoded frames {s.get("nb_read_frames")} != {c["frames"]}')
        t = json.load(open(sidecar))
        if t.get('case_id') != c['id']: failures.append(f'{c["id"]}: sidecar case mismatch')
        if t.get('output', {}).get('decoded_frames') not in (0, c['frames']):
            failures.append(f'{c["id"]}: sidecar decoded frame mismatch')
        decode = subprocess.run(
            ['ffmpeg', '-v', 'error', '-i', str(mp4), '-f', 'null', '-'],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if decode.returncode != 0:
            failures.append(f'{c["id"]}: full decode failed: {decode.stderr.strip()[:160]}')
    except Exception as exc:
        failures.append(f'{c["id"]}: probe/telemetry error: {exc}')
if failures:
    for failure in failures: print(f'GATE_FAIL: check_final_video_suite: {failure}', file=sys.stderr)
    raise SystemExit(1)
print(f'GATE_PASS: check_final_video_suite: {len(manifest["cases"])}/{len(manifest["cases"])} MP4s verified')
print('[INFO] check_final_video_suite: ffprobe frame-count, resolution, fps and telemetry sidecars verified')
PY
