#!/usr/bin/env bash
# Visual correctness gate for the DirectYUV prepared-MTSDF text route.
#
# Zero-copy counters prove ownership and transfer behaviour; they do not prove
# that the authored overlay survived lowering. This gate compares the encoded
# frame with the corresponding source frame inside the declared text ROI.

set -euo pipefail

SOURCE=""
OUTPUT=""
REPORT=""
COMMIT_SHA=""
X=100
Y=100
W=1080
H=220

while [[ $# -gt 0 ]]; do
    case "$1" in
        --source) SOURCE="$2"; shift 2 ;;
        --output) OUTPUT="$2"; shift 2 ;;
        --report) REPORT="$2"; shift 2 ;;
        --commit-sha) COMMIT_SHA="$2"; shift 2 ;;
        --roi) IFS=, read -r X Y W H <<< "$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ -z "$SOURCE" || -z "$OUTPUT" ]]; then
    echo "usage: $0 --source <source.mp4> --output <encoded.mp4> [--report <json>] [--commit-sha <sha>] [--roi x,y,w,h]" >&2
    exit 2
fi

TMP_DIR="$(mktemp -d /tmp/chronon-direct-yuv-visual.XXXXXX)"
cleanup() {
    rm -f "$TMP_DIR/source.rgb" "$TMP_DIR/output.rgb"
    rmdir "$TMP_DIR" 2>/dev/null || true
}
trap cleanup EXIT

ffmpeg -hide_banner -loglevel error -y \
    -i "$SOURCE" -frames:v 1 -pix_fmt rgb24 -f rawvideo "$TMP_DIR/source.rgb"
ffmpeg -hide_banner -loglevel error -y \
    -i "$OUTPUT" -frames:v 1 -pix_fmt rgb24 -f rawvideo "$TMP_DIR/output.rgb"

python3 - "$TMP_DIR/source.rgb" "$TMP_DIR/output.rgb" "$REPORT" "$COMMIT_SHA" "$X" "$Y" "$W" "$H" "$SOURCE" "$OUTPUT" <<'PY'
import json
import pathlib
import sys

source_path, output_path, report_path, commit_sha, sx, sy, sw, sh, source_media, output_media = sys.argv[1:]
source = pathlib.Path(source_path).read_bytes()
output = pathlib.Path(output_path).read_bytes()
width, height = 1280, 720
x, y, w, h = map(int, (sx, sy, sw, sh))
expected = width * height * 3
if len(source) != expected or len(output) != expected:
    raise SystemExit(f"VISUAL_BLOCKED: expected {expected} bytes, got source={len(source)} output={len(output)}")

values = []
changed = 0
for row in range(y, min(y + h, height)):
    for col in range(x, min(x + w, width)):
        index = (row * width + col) * 3
        delta = (abs(source[index] - output[index]) +
                 abs(source[index + 1] - output[index + 1]) +
                 abs(source[index + 2] - output[index + 2]))
        values.append(delta)
        if delta > 30:
            changed += 1

values.sort()
count = len(values)
mean_delta = sum(values) / count if count else 0.0
p95_delta = values[int(count * 0.95)] if count else 0
# The source and encoded frame naturally differ by codec noise. An authored
# high-contrast text overlay must nevertheless create a large, localized delta.
passed = mean_delta >= 25.0 and changed >= 5000
result = {
    "schema": "chronon.direct-yuv-text-visual.v1",
    "commit_sha": commit_sha,
    "source": source_media,
    "output": output_media,
    "roi": {"x": x, "y": y, "width": w, "height": h},
    "mean_rgb_abs_delta": mean_delta,
    "p95_rgb_abs_delta": p95_delta,
    "pixels_delta_gt_30": changed,
    "pixels_in_roi": count,
    "result": "PASS" if passed else "FAIL",
}
if report_path:
    pathlib.Path(report_path).write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, sort_keys=True))
if not passed:
    raise SystemExit("VISUAL_FAIL: DirectYUV output has no certified text overlay delta")
PY

echo "DIRECT_YUV_TEXT_VISUAL_PASS"
