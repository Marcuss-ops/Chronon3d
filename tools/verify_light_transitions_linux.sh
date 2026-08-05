#!/usr/bin/env bash
# Verify the smallest product-level light-transition + SFX contract.
#
# The visual composition and the audio track are intentionally separate:
# Chronon3D renders the composition through SceneBuilder::clip_transition,
# while this harness reads the canonical Render Plan audio-track fixture and
# muxes that cue onto the rendered video.  No production API is added here.

set -Eeuo pipefail

GATE_NAME=verify_light_transitions_linux
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CLI=${CLI:-"$ROOT/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli"}
PLAN=${PLAN:-"$ROOT/examples/light_transition_sound_smoke_audio.json"}
OUTPUT_DIR=${OUTPUT_DIR:-"$ROOT/output/light_transition"}
COMPOSITION=LightTransitionSoundSmoke
FPS=30
FRAME_COUNT=60
TRANSITION_FROM=20
TRANSITION_DURATION=12
TRANSITION_OFFSET=0.6666667
VIDEO_DURATION=2.0

if [[ "$CLI" != /* ]]; then CLI="$ROOT/${CLI#./}"; fi
if [[ "$PLAN" != /* ]]; then PLAN="$ROOT/${PLAN#./}"; fi
if [[ "$OUTPUT_DIR" != /* ]]; then OUTPUT_DIR="$ROOT/${OUTPUT_DIR#./}"; fi

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/chronon-light-transition.XXXXXX")
trap 'rm -rf "$WORK_DIR"' EXIT

fail() {
    echo "CHRONON_LIGHT_TRANSITIONS_FAIL: $*" >&2
    exit 1
}

require_file() {
    [[ -f "$1" ]] || fail "missing file: $1"
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

require_command ffmpeg
require_command ffprobe
require_command sha256sum
require_command python3
require_command convert
require_command identify
[[ -x "$CLI" ]] || fail "CLI is not executable: $CLI"
require_file "$PLAN"

audio_meta=$(python3 - "$PLAN" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    plan = json.load(handle)
tracks = plan.get("audio_tracks", [])
if len(tracks) != 1:
    raise SystemExit("expected exactly one audio track")
track = tracks[0]
print(track.get("source", ""))
print(track.get("volume", ""))
print(track.get("start_time_offset", ""))
print(track.get("duration_seconds", ""))
print(track.get("role", ""))
PY
) || fail "audio plan is not valid JSON with one audio track"
mapfile -t AUDIO_META <<< "$audio_meta"
[[ "${#AUDIO_META[@]}" -eq 5 ]] || fail "audio plan metadata is incomplete"
AUDIO_SOURCE=${AUDIO_META[0]}
AUDIO_VOLUME=${AUDIO_META[1]}
AUDIO_OFFSET=${AUDIO_META[2]}
AUDIO_DURATION=${AUDIO_META[3]}
AUDIO_ROLE=${AUDIO_META[4]}
AUDIO_FILE="$ROOT/$AUDIO_SOURCE"

require_file "$AUDIO_FILE"
[[ "$AUDIO_ROLE" == transition_sfx ]] || fail "audio role is not transition_sfx: $AUDIO_ROLE"
awk -v actual="$AUDIO_OFFSET" -v expected="$TRANSITION_OFFSET" \
    'BEGIN { exit !(actual >= expected - 0.00001 && actual <= expected + 0.00001) }' \
    || fail "audio offset does not equal frame 20 / 30: $AUDIO_OFFSET"
awk -v actual="$AUDIO_DURATION" 'BEGIN { exit !(actual > 0.0 && actual <= 0.5) }' \
    || fail "audio duration is invalid: $AUDIO_DURATION"

mkdir -p "$OUTPUT_DIR"

render_sequence() {
    local scheduler=$1
    local workers=$2
    local threads=$3
    local destination=$4
    local warmup=$5
    mkdir -p "$destination"
    local -a extra=()
    if [[ "$warmup" == 1 ]]; then
        extra+=(--warmup-renderer --warmup-dummy-frame)
    fi
    env \
        "CHRONON3D_SCHEDULER_MODE=$scheduler" \
        "CHRONON3D_SCHEDULER_WORKERS=$workers" \
        "CHRONON3D_THREADS=$threads" \
        "$CLI" render "$COMPOSITION" \
        --frames "0-$((FRAME_COUNT - 1))" \
        --assets-root "$ROOT" \
        --profile production \
        "${extra[@]}" \
        -o "$destination/frame_####.png" \
        >/dev/null 2>"$destination/render.log" \
        || fail "CLI render failed: scheduler=$scheduler warmup=$warmup"
}

render_frame_random_order() {
    local destination=$1
    mkdir -p "$destination"
    # Deliberately non-sequential access to the seven contract frames.
    local -a order=(31 19 29 20 32 23 26)
    for frame in "${order[@]}"; do
        env \
            CHRONON3D_SCHEDULER_MODE=sequential \
            CHRONON3D_SCHEDULER_WORKERS=1 \
            CHRONON3D_THREADS=1 \
            "$CLI" render "$COMPOSITION" \
            --frame "$frame" \
            --assets-root "$ROOT" \
            --profile production \
            -o "$destination/frame_$(printf '%04d' "$frame").png" \
            >/dev/null 2>"$destination/render_${frame}.log" \
            || fail "CLI random-access render failed at frame $frame"
    done
}

hash_manifest() {
    local source_dir=$1
    local manifest=$2
    mapfile -t files < <(find "$source_dir" -maxdepth 1 -type f -name 'frame_*.png' -printf '%f\n' | sort)
    [[ "${#files[@]}" -eq "$FRAME_COUNT" ]] \
        || fail "expected $FRAME_COUNT PNG frames in $source_dir, found ${#files[@]}"
    : > "$manifest"
    for file in "${files[@]}"; do
        sha256sum "$source_dir/$file" | awk -v name="$file" '{ print $1 "  " name }' >> "$manifest"
    done
}

frame_path() {
    printf '%s/frame_%04d.png' "$1" "$2"
}

pixel_values_at() {
    local image=$1
    local x=$2
    local y=$3
    convert "$image" -format "%[fx:p{$x,$y}.r] %[fx:p{$x,$y}.g] %[fx:p{$x,$y}.b] %[fx:p{$x,$y}.a]\n" info:
}

assert_pixel_near_at() {
    local image=$1
    local x=$2
    local y=$3
    local expected_r=$4
    local expected_g=$5
    local expected_b=$6
    local epsilon=$7
    local values
    values=$(pixel_values_at "$image" "$x" "$y") || fail "cannot inspect pixel: $image ($x,$y)"
    read -r r g b a <<< "$values"
    awk -v r="$r" -v g="$g" -v b="$b" -v a="$a" \
        -v er="$expected_r" -v eg="$expected_g" -v eb="$expected_b" \
        -v e="$epsilon" \
        'BEGIN { exit !(a >= 0.0 && a <= 1.0 && r >= er-e && r <= er+e && g >= eg-e && g <= eg+e && b >= eb-e && b <= eb+e) }' \
        || fail "unexpected pixel at ($x,$y) in $image: $values"
}

assert_not_black() {
    local image=$1
    local mean
    mean=$(convert "$image" -colorspace gray -format '%[fx:mean]' info:) \
        || fail "cannot inspect luma: $image"
    awk -v value="$mean" 'BEGIN { exit !(value > 0.001) }' \
        || fail "black frame detected: $image"
}

assert_alpha_range() {
    local image=$1
    local values
    values=$(convert "$image" -format '%[fx:minima.a] %[fx:maxima.a]\n' info:) \
        || fail "cannot inspect alpha: $image"
    read -r alpha_min alpha_max <<< "$values"
    awk -v lo="$alpha_min" -v hi="$alpha_max" \
        'BEGIN { exit !(lo >= 0.0 && hi <= 1.0 && lo == lo && hi == hi) }' \
        || fail "invalid alpha range in $image: $values"
}

echo "[INFO] $GATE_NAME: rendering serial cold baseline"
render_sequence sequential 1 1 "$WORK_DIR/run_a" 0
echo "[INFO] $GATE_NAME: rendering serial cold repeat"
render_sequence sequential 1 1 "$WORK_DIR/run_b" 0
echo "[INFO] $GATE_NAME: rendering parallel and warm-cache variants"
render_sequence fixed 4 4 "$WORK_DIR/run_parallel" 0
render_sequence sequential 1 1 "$WORK_DIR/run_warm" 1
render_frame_random_order "$WORK_DIR/run_random"

hash_manifest "$WORK_DIR/run_a" "$WORK_DIR/run_a.sha256"
hash_manifest "$WORK_DIR/run_b" "$WORK_DIR/run_b.sha256"
hash_manifest "$WORK_DIR/run_parallel" "$WORK_DIR/run_parallel.sha256"
hash_manifest "$WORK_DIR/run_warm" "$WORK_DIR/run_warm.sha256"
cmp -s "$WORK_DIR/run_a.sha256" "$WORK_DIR/run_b.sha256" \
    || fail "serial cold renders are not byte-identical"
cmp -s "$WORK_DIR/run_a.sha256" "$WORK_DIR/run_parallel.sha256" \
    || fail "serial and parallel renders differ"
cmp -s "$WORK_DIR/run_a.sha256" "$WORK_DIR/run_warm.sha256" \
    || fail "cold and warm-cache renders differ"

for frame in 19 20 23 26 29 31 32; do
    image=$(frame_path "$WORK_DIR/run_a" "$frame")
    require_file "$image"
    identify -format '%wx%h' "$image" | grep -qx '1920x1080' \
        || fail "wrong dimensions at frame $frame"
    assert_not_black "$image"
    assert_alpha_range "$image"
    cp "$image" "$OUTPUT_DIR/frame_${frame}.png"
done

cmp -s "$(frame_path "$WORK_DIR/run_a" 19)" "$(frame_path "$WORK_DIR/run_a" 20)" \
    || fail "frame 20 does not begin at the declared half-open boundary"
# Corner probes avoid the centered scene labels; frame 26 uses the
# integration test's canonical center probe for the flare peak.
assert_pixel_near_at "$(frame_path "$WORK_DIR/run_a" 19)" 0 0 0.03 0.08 0.22 0.04
assert_pixel_near_at "$(frame_path "$WORK_DIR/run_a" 20)" 0 0 0.03 0.08 0.22 0.04
assert_pixel_near_at "$(frame_path "$WORK_DIR/run_a" 26)" 960 540 1.0 1.0 1.0 0.02
assert_pixel_near_at "$(frame_path "$WORK_DIR/run_a" 32)" 0 0 0.28 0.03 0.04 0.04

for frame in 19 20 23 26 29 31 32; do
    random_image=$(frame_path "$WORK_DIR/run_random" "$frame")
    cmp -s "$random_image" "$(frame_path "$WORK_DIR/run_a" "$frame")" \
        || fail "random-order frame differs at frame $frame"
done

echo "[INFO] $GATE_NAME: muxing the independent transition_sfx cue"
delay_ms=$(awk -v offset="$AUDIO_OFFSET" 'BEGIN { printf "%.0f", offset * 1000.0 }')
SILENT_VIDEO="$WORK_DIR/light_transition_silent.mp4"
FINAL_VIDEO="$WORK_DIR/light_transition_sound.mp4"
env CHRONON3D_SCHEDULER_MODE=sequential CHRONON3D_SCHEDULER_WORKERS=1 CHRONON3D_THREADS=1 \
    "$CLI" render "$COMPOSITION" \
    --frames "0-$((FRAME_COUNT - 1))" \
    --assets-root "$ROOT" \
    --profile production \
    -o "$SILENT_VIDEO" \
    >/dev/null 2>"$WORK_DIR/video_render.log" \
    || fail "CLI video render failed"

ffmpeg -hide_banner -loglevel error -y \
    -i "$SILENT_VIDEO" -i "$AUDIO_FILE" \
    -filter_complex "[1:a]volume=${AUDIO_VOLUME},adelay=${delay_ms}:all=1,apad,atrim=duration=${VIDEO_DURATION}[aout]" \
    -map 0:v:0 -map '[aout]' -c:v copy -c:a aac -t "$VIDEO_DURATION" \
    -movflags +faststart "$FINAL_VIDEO" \
    || fail "FFmpeg audio mux failed"

cp "$FINAL_VIDEO" "$OUTPUT_DIR/light_transition_sound.mp4"
ffprobe -v error \
    -show_entries format=duration \
    -show_entries stream=index,codec_type,codec_name,duration \
    -of json "$OUTPUT_DIR/light_transition_sound.mp4" \
    > "$OUTPUT_DIR/ffprobe.json" \
    || fail "ffprobe could not inspect the final MP4"

stream_count=$(ffprobe -v error -select_streams a -show_entries stream=index -of csv=p=0 \
    "$OUTPUT_DIR/light_transition_sound.mp4" | sed '/^[[:space:]]*$/d' | wc -l)
[[ "$stream_count" -eq 1 ]] || fail "final MP4 must contain exactly one audio stream"

video_duration=$(ffprobe -v error -select_streams v:0 -show_entries stream=duration -of csv=p=0 \
    "$OUTPUT_DIR/light_transition_sound.mp4")
audio_duration=$(ffprobe -v error -select_streams a:0 -show_entries stream=duration -of csv=p=0 \
    "$OUTPUT_DIR/light_transition_sound.mp4")
awk -v value="$video_duration" 'BEGIN { exit !(value >= 1.98 && value <= 2.02) }' \
    || fail "video duration is not 2 seconds: $video_duration"
awk -v value="$audio_duration" 'BEGIN { exit !(value >= 1.98 && value <= 2.02) }' \
    || fail "audio duration is not equal to video duration: $audio_duration"

EXTRACTED_AUDIO="$OUTPUT_DIR/extracted_audio.wav"
ffmpeg -hide_banner -loglevel error -y \
    -i "$OUTPUT_DIR/light_transition_sound.mp4" -map 0:a:0 -ac 1 -ar 44100 \
    "$EXTRACTED_AUDIO" \
    || fail "could not extract final audio"
ffmpeg -hide_banner -loglevel error -y \
    -i "$EXTRACTED_AUDIO" -filter_complex 'showwavespic=s=1920x400' -frames:v 1 \
    "$OUTPUT_DIR/waveform.png" \
    || fail "could not generate the audio waveform"

RAW_AUDIO="$WORK_DIR/extracted_audio.raw"
ffmpeg -hide_banner -loglevel error -y \
    -i "$EXTRACTED_AUDIO" -f s16le -ac 1 -ar 44100 "$RAW_AUDIO" \
    || fail "could not decode extracted audio samples"
onset=$(python3 - "$RAW_AUDIO" <<'PY'
import array
import math
import sys

with open(sys.argv[1], "rb") as handle:
    samples = array.array("h")
    samples.frombytes(handle.read())
window = 441  # 10 ms at 44.1 kHz
threshold = 300.0
for start in range(0, max(0, len(samples) - window), 10):
    chunk = samples[start:start + window]
    rms = math.sqrt(sum(value * value for value in chunk) / len(chunk))
    if rms >= threshold:
        print(start / 44100.0)
        break
else:
    raise SystemExit("no significant audio onset found")
PY
) || fail "could not find a significant audio onset"
awk -v actual="$onset" -v expected="$AUDIO_OFFSET" \
    'BEGIN { delta = actual - expected; if (delta < 0) delta = -delta; exit !(delta <= (1.0 / 30.0)) }' \
    || fail "audio onset ${onset}s is outside the +/-1 frame window from ${AUDIO_OFFSET}s"

echo "CHRONON_LIGHT_TRANSITIONS_PASS"
echo "[INFO] $GATE_NAME: 7 key frames, deterministic scheduler/cache matrix, one synced audio stream"
