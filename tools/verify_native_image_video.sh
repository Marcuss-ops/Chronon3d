#!/usr/bin/env bash
# Certification gate for the Vulkan native-image video route.
# This is intentionally separate from DirectYUV conversion-zero: an authored
# image is uploaded once to the GPU and the Vulkan graph composites it into the
# native video surface before NVENC.

set -euo pipefail

REPORT=""
OUTPUT=""
COMMIT_SHA=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --report) REPORT="$2"; shift 2 ;;
        --output) OUTPUT="$2"; shift 2 ;;
        --commit-sha) COMMIT_SHA="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [[ -z "$REPORT" || -z "$OUTPUT" ]]; then
    echo "usage: $0 --report <timing.json> --output <video.mp4> [--commit-sha <sha>]" >&2
    exit 2
fi

TMP_DIR="$(mktemp -d /tmp/chronon-native-image.XXXXXX)"
cleanup() {
    rm -f "$TMP_DIR/frame.rgb"
    rmdir "$TMP_DIR" 2>/dev/null || true
}
trap cleanup EXIT

ffmpeg -hide_banner -loglevel error -y -i "$OUTPUT" -frames:v 1 \
    -pix_fmt rgb24 -f rawvideo "$TMP_DIR/frame.rgb"

python3 - "$REPORT" "$TMP_DIR/frame.rgb" "$OUTPUT" "$COMMIT_SHA" <<'PY'
import json
import pathlib
import sys

report_path, frame_path, output_path, commit_sha = sys.argv[1:]
data = json.loads(pathlib.Path(report_path).read_text())
job = data.get("job", {})
gpu = job.get("gpu", {})
required = {
    "execution_path": "full_graph_native",
    "surface_handoff_path": "direct",
    "effective_backend": "vulkan",
    "encoder_backend": "nvenc",
}
for field, expected in required.items():
    actual = job.get(field)
    if actual != expected:
        raise SystemExit(f"NATIVE_IMAGE_FAIL: {field}={actual!r}, expected {expected!r}")

checks = {
    "gpu_native_surface_frames": gpu.get("gpu_native_surface_frames", 0) > 0,
    "gpu_native_encode_frames": gpu.get("gpu_native_encode_frames", 0) > 0,
    "cpu_readback_bytes": gpu.get("cpu_pixel_readback_bytes", 0) == 0,
    "cpu_full_surface_upload_bytes": gpu.get("cpu_full_surface_upload_bytes", 0) == 0,
    "video_surface_upload_bytes": gpu.get("video_surface_upload_bytes", 0) == 0,
    "software_fallback_nodes": gpu.get("software_fallback_nodes", 0) == 0,
    "image_upload_once": gpu.get("upload_breakdown", {}).get("gpu_upload_image_full_count") == 1,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("NATIVE_IMAGE_FAIL: " + ", ".join(failed))

frame = pathlib.Path(frame_path).read_bytes()
width, height = 1280, 720
def pixel(x, y):
    index = (y * width + x) * 3
    return frame[index:index + 3]

# assets/test_image.png is a four-quadrant red/green/blue/yellow canary,
# centered by the fixture at x=640,y=360. Allow NVENC quantisation drift.
expected = {
    "red": ((560, 300), (180, 80, 80)),
    "green": ((720, 300), (80, 180, 80)),
    "blue": ((560, 420), (80, 80, 180)),
    "yellow": ((720, 420), (180, 180, 80)),
}
observed = {name: tuple(pixel(*point)) for name, (point, _) in expected.items()}
for name, (point, tolerance) in expected.items():
    value = observed[name]
    if any(abs(value[i] - ((255, 0, 0, 0)[i] if name == "red" else
                           (0, 255, 0, 0)[i] if name == "green" else
                           (0, 0, 255, 0)[i] if name == "blue" else
                           (255, 255, 0, 0)[i])) > tolerance[i]
               for i in range(3)):
        raise SystemExit(f"NATIVE_IMAGE_FAIL: {name} sample={value} point={point}")

result = {
    "schema": "chronon.native-image-video.v1",
    "commit_sha": commit_sha,
    "report": report_path,
    "output": output_path,
    "route": required,
    "gpu": {key: gpu.get(key) for key in (
        "gpu_native_surface_frames", "gpu_native_encode_frames",
        "gpu_upload_bytes", "nv12_to_rgba_frames", "rgba_to_nv12_frames")},
    "image_samples": observed,
    "result": "PASS",
}
print(json.dumps(result, sort_keys=True))
PY

echo "NATIVE_IMAGE_VIDEO_PASS"
