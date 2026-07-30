#!/usr/bin/env bash
# Blocking product gate for the combined Glow + Camera 2.5D V1 fixture.
#
# Exit codes: 0 PASS, 1 FAIL, 2 BLOCKED.
set -uo pipefail

GATE_NAME="verify_glow_camera_product_v1"
ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
BUILD_DIR="${CHRONON3D_COMBINED_BUILD_DIR:-build/chronon/linux-ci-full-validation}"
CLI="$ROOT/$BUILD_DIR/apps/chronon3d_cli/chronon3d_cli"
OUT="${CHRONON3D_COMBINED_OUTPUT_DIR:-$(mktemp -d /tmp/chronon3d-glow-camera-v1.XXXXXX)}"
SDK_PREFIX="${CHRONON3D_COMBINED_SDK_PREFIX:-$(mktemp -d /tmp/chronon3d-glow-camera-sdk.XXXXXX)}"

fail() { echo "GATE_FAIL: $*"; exit 1; }
blocked() { echo "GATE_BLOCKED: $*"; exit 2; }
run() { echo "+ $*"; "$@" || fail "command failed: $*"; }

cd "$ROOT" || blocked "cannot enter repository root"
export CHRONON3D_CLI_ASSETS_ROOT="$ROOT"
[ -x "$CLI" ] || blocked "CLI not found: $CLI"
command -v ffprobe >/dev/null 2>&1 || blocked "ffprobe is required"
command -v identify >/dev/null 2>&1 || blocked "ImageMagick identify is required"
mkdir -p "$OUT"

render_frame() {
    local comp="$1" frame="$2" output="$3"
    run "$CLI" render "$comp" --frame "$frame" --output "$output" --profile production --log-level error
}

check_png() {
    local file="$1" expected="$2"
    [ -s "$file" ] || fail "missing PNG: $file"
    local geometry ink
    geometry="$(identify -format '%wx%h' "$file")" || fail "cannot inspect PNG: $file"
    [ "$geometry" = "$expected" ] || fail "$file has geometry $geometry, expected $expected"
    ink="$(identify -format '%[fx:mean]' "$file")"
    [ "$ink" != "0" ] || fail "$file is empty/transparent"
}

declare -A hashes
for frame in 0 15 30 45 60; do
    file="$OUT/landscape-cold-$frame.png"
    render_frame GlowCameraProductV1 "$frame" "$file"
    check_png "$file" 1920x1080
    hashes[$frame]="$(sha256sum "$file" | awk '{print $1}')"
done

# The same frames in a different order are separate jobs and therefore test
# random-access determinism without relying on a warmed process.
for frame in 60 0 30 15 45; do
    file="$OUT/landscape-random-$frame.png"
    render_frame GlowCameraProductV1 "$frame" "$file"
    check_png "$file" 1920x1080
    actual="$(sha256sum "$file" | awk '{print $1}')"
    [ "$actual" = "${hashes[$frame]}" ] || fail "random-access hash mismatch at frame $frame"
done

run "$CLI" preview GlowCameraProductV1 --frames 0,15,30,45,60 --output-dir "$OUT/warm"
for frame in 0 15 30 45 60; do
    printf -v warm_frame '%s/warm/frame_%04d.png' "$OUT" "$frame"
    check_png "$warm_frame" 1920x1080
done

run "$CLI" render GlowCameraProductV1 --frames 0-60 --chunks 2 --output "$OUT/parallel-####.png" --profile production --log-level error
check_png "$OUT/parallel-0030.png" 1920x1080
[ "$(sha256sum "$OUT/parallel-0030.png" | awk '{print $1}')" = "${hashes[30]}" ] || fail "parallel hash mismatch at frame 30"

run "$CLI" render GlowCameraProductV1 --frames 0-59 --fps 30 --output "$OUT/combined.mp4" --profile production --log-level error
video_meta="$(ffprobe -v error -count_frames -select_streams v:0 \
    -show_entries stream=width,height,nb_read_frames -of csv=p=0 "$OUT/combined.mp4")"
[ "$video_meta" = "1920,1080,60" ] || fail "landscape video metadata mismatch: $video_meta"

for frame in 0 30 60; do
    file="$OUT/portrait-$frame.png"
    render_frame GlowCameraProductV1Portrait "$frame" "$file"
    check_png "$file" 1080x1920
done

# Installed SDK and C ABI checks are mandatory evidence for the same source
# revision. The install consumer exercises the exported C++ package; the C
# ABI suite validates the canonical render-plan decoder/validator contract.
run env CHRONON3D_INSTALL_TEST_PRESET="${CHRONON3D_INSTALL_TEST_PRESET:-linux-ci-full-validation}" \
    CHRONON3D_INSTALL_TEST_FAST="${CHRONON3D_INSTALL_TEST_FAST:-0}" \
    CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-2}" \
    SDK_BUILD="$ROOT/$BUILD_DIR" SDK_PREFIX="$SDK_PREFIX" bash tools/install_consumer_test.sh
run ctest --test-dir "$BUILD_DIR" -R '^chronon3d_c_abi_tests$' --output-on-failure

echo "GATE_PASS: $GATE_NAME — combined GlowCameraProductV1 PASS"
echo "[INFO] ${GATE_NAME}: landscape+portrait, 60-frame video, deterministic cold/warm/random/parallel and SDK/C ABI checks passed"
