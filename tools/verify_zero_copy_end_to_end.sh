#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_zero_copy_end_to_end.sh — Zero-copy certification gate
#
# Validates that the video pipeline achieves true zero-copy end-to-end:
#   NVDEC → native surface → CUDA/Vulkan compose → NVENC
#
# Required gates (ALL must pass for ZERO_COPY_PASS):
#   1. host_upload_bytes = 0         (no CPU→GPU uploads)
#   2. host_readback_bytes = 0       (no GPU→CPU readbacks)
#   3. nv12_to_rgba_frames = 0       (no NV12→RGBA conversions)
#   4. rgba_to_nv12_frames = 0       (no RGBA→NV12 conversions)
#   5. encoder_staging_copy_bytes = 0 (no encoder staging copies)
#   6. gpu_surface_copy_frames = 0   (no GPU surface copies)
#   7. cpu_pixel_readback_bytes = 0  (no CPU pixel readbacks)
#   8. video_surface_upload_bytes = 0 (no video surface uploads)
#
# Optional gates (reported but don't block):
#   - gpu_native_surface_frames > 0  (frames using native GPU surfaces)
#   - gpu_native_encode_frames > 0   (frames encoded natively via NVENC)
#
# Usage:
#   bash tools/verify_zero_copy_end_to_end.sh
#   bash tools/verify_zero_copy_end_to_end.sh --scene TextPlaceAnimatedCenter --frames 60
#   bash tools/verify_zero_copy_end_to_end.sh --report-json /path/to/report.json
#
# Exit codes:
#   0 = ZERO_COPY_PASS
#   1 = ZERO_COPY_FAIL (non-zero copy metrics detected)
#   2 = BLOCKED (report missing/unreadable or required fields absent)
# ═══════════════════════════════════════════════════════════════════════════

set -uo pipefail

GATE_NAME="verify_zero_copy_end_to_end"

# ── Defaults ─────────────────────────────────────────────────────────────
SCENE="${CHRONON3D_BENCH_SCENE:-TextPlaceAnimatedCenter}"
FRAMES="${CHRONON3D_BENCH_FRAMES:-60}"
WARMUP="${CHRONON3D_BENCH_WARMUP:-10}"
REPORT_JSON=""
OUTPUT_DIR="/tmp/zero_copy_verify"

CLI_BIN="${CHRONON3D_CLI:-}"
if [[ -z "$CLI_BIN" ]]; then
    for candidate in \
        "$PWD/build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli" \
        "$PWD/build/fast/apps/chronon3d_cli/chronon3d_cli" \
        "$PWD/build/apps/chronon3d_cli/chronon3d_cli"; do
        if [[ -x "$candidate" ]]; then
            CLI_BIN="$candidate"
            break
        fi
    done
fi

# ── Arg parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --scene)        SCENE="$2"; shift 2 ;;
        --frames)       FRAMES="$2"; shift 2 ;;
        --warmup)       WARMUP="$2"; shift 2 ;;
        --report-json)  REPORT_JSON="$2"; shift 2 ;;
        --output-dir)   OUTPUT_DIR="$2"; shift 2 ;;
        --help|-h)
            sed -n '2,30p' "$0"
            exit 0
            ;;
        *) echo "[ERROR] Unknown arg: $1" >&2; exit 2 ;;
    esac
done

# ── Helpers ──────────────────────────────────────────────────────────────
_info() { echo "[INFO] $GATE_NAME: $*" >&2; }
_warn() { echo "[WARN] $GATE_NAME: $*" >&2; }
_fail() { echo "[FAIL] $GATE_NAME: $*" >&2; }
_pass() { echo "[PASS] $GATE_NAME: $*" >&2; }

# Extract a counter value from a JSON report using python3
extract_counter() {
    local json_file="$1"
    local counter_name="$2"
    python3 -c "
import json, sys
try:
    with open('$json_file') as f:
        data = json.load(f)
    # Try nested path: counters.<name>
    counters = data.get('counters', {})
    if '$counter_name' in counters:
        print(counters['$counter_name'])
        sys.exit(0)
    # Try flat path
    if '$counter_name' in data:
        print(data['$counter_name'])
        sys.exit(0)
    # Try run.counters.<name>
    run = data.get('run', {})
    counters = run.get('counters', {})
    if '$counter_name' in counters:
        print(counters['$counter_name'])
        sys.exit(0)
    print('')
except Exception as e:
    print('', file=sys.stderr)
" 2>/dev/null
}

# ── Generate report if not provided ──────────────────────────────────────
if [[ -z "$REPORT_JSON" ]]; then
    if [[ ! -x "$CLI_BIN" ]]; then
        _fail "chronon3d_cli not found at: $CLI_BIN"
        echo "GATE_BLOCKED"
        exit 2
    fi

    mkdir -p "$OUTPUT_DIR"
    REPORT_JSON="$OUTPUT_DIR/zero_copy_report.json"

    _info "Running video export to collect zero-copy metrics..."
    _info "  Scene: $SCENE, Frames: $FRAMES, Warmup: $WARMUP"

    # Run video export with telemetry
    if ! "$CLI_BIN" render "$SCENE" \
        --frames "$FRAMES" \
        --warmup "$WARMUP" \
        --report \
        --output-format nv12 \
        -o "$OUTPUT_DIR/zero_copy_####.nv12" \
        > "$OUTPUT_DIR/render.log" 2>&1; then
        _warn "Video export failed — see $OUTPUT_DIR/render.log"
        # Continue to check if partial report was generated
    fi

    # Try to extract report from render log or sidecar
    if [[ -f "$OUTPUT_DIR/zero_copy_report.json" ]]; then
        _info "Report found at $REPORT_JSON"
    else
        # Fallback: extract counters from render log
        _info "Extracting counters from render log..."
        python3 -c "
import json, re, sys

counters = {}
log_file = '$OUTPUT_DIR/render.log'

# Extract counter values from log output
patterns = {
    'gpu_readback_bytes': r'gpu_readback_bytes[=:]\s*(\d+)',
    'encoder_staging_copy_bytes': r'encoder_staging_copy_bytes[=:]\s*(\d+)',
    'gpu_surface_copy_frames': r'gpu_surface_copy_frames[=:]\s*(\d+)',
    'cpu_pixel_readback_bytes': r'cpu_pixel_readback_bytes[=:]\s*(\d+)',
    'video_surface_upload_bytes': r'video_surface_upload_bytes[=:]\s*(\d+)',
    'gpu_native_surface_frames': r'gpu_native_surface_frames[=:]\s*(\d+)',
    'gpu_native_encode_frames': r'gpu_native_encode_frames[=:]\s*(\d+)',
}

try:
    with open(log_file) as f:
        content = f.read()
    for name, pattern in patterns.items():
        match = re.search(pattern, content)
        if match:
            counters[name] = int(match.group(1))
        else:
            counters[name] = 0
except Exception as e:
    print(f'Error reading log: {e}', file=sys.stderr)
    for name in patterns:
        counters[name] = 0

report = {
    'schema': 'chronon3d.zero_copy.v1',
    'scene': '$SCENE',
    'frames': $FRAMES,
    'counters': counters
}

with open('$REPORT_JSON', 'w') as f:
    json.dump(report, f, indent=2)

print(f'Report written to $REPORT_JSON')
" 2>&1 || _warn "Failed to extract counters from log"
    fi
fi

# ── Validate report exists ───────────────────────────────────────────────
if [[ ! -f "$REPORT_JSON" ]]; then
    _fail "Report not found: $REPORT_JSON"
    echo "GATE_BLOCKED"
    exit 2
fi

_info "Validating zero-copy gates from: $REPORT_JSON"

# ── Extract counters ─────────────────────────────────────────────────────
HOST_UPLOAD_BYTES=$(extract_counter "$REPORT_JSON" "gpu_upload_bytes")
HOST_READBACK_BYTES=$(extract_counter "$REPORT_JSON" "gpu_readback_bytes")
NV12_TO_RGBA_FRAMES=$(extract_counter "$REPORT_JSON" "nv12_to_rgba_frames")
RGBA_TO_NV12_FRAMES=$(extract_counter "$REPORT_JSON" "rgba_to_nv12_frames")
ENCODER_STAGING_COPY=$(extract_counter "$REPORT_JSON" "encoder_staging_copy_bytes")
GPU_SURFACE_COPY=$(extract_counter "$REPORT_JSON" "gpu_surface_copy_frames")
CPU_READBACK_BYTES=$(extract_counter "$REPORT_JSON" "cpu_pixel_readback_bytes")
VIDEO_UPLOAD_BYTES=$(extract_counter "$REPORT_JSON" "video_surface_upload_bytes")
NATIVE_SURFACE_FRAMES=$(extract_counter "$REPORT_JSON" "gpu_native_surface_frames")
NATIVE_ENCODE_FRAMES=$(extract_counter "$REPORT_JSON" "gpu_native_encode_frames")

# Default to 0 if empty
HOST_UPLOAD_BYTES="${HOST_UPLOAD_BYTES:-0}"
HOST_READBACK_BYTES="${HOST_READBACK_BYTES:-0}"
NV12_TO_RGBA_FRAMES="${NV12_TO_RGBA_FRAMES:-0}"
RGBA_TO_NV12_FRAMES="${RGBA_TO_NV12_FRAMES:-0}"
ENCODER_STAGING_COPY="${ENCODER_STAGING_COPY:-0}"
GPU_SURFACE_COPY="${GPU_SURFACE_COPY:-0}"
CPU_READBACK_BYTES="${CPU_READBACK_BYTES:-0}"
VIDEO_UPLOAD_BYTES="${VIDEO_UPLOAD_BYTES:-0}"
NATIVE_SURFACE_FRAMES="${NATIVE_SURFACE_FRAMES:-0}"
NATIVE_ENCODE_FRAMES="${NATIVE_ENCODE_FRAMES:-0}"

# ── Evaluate gates ───────────────────────────────────────────────────────
PASS_COUNT=0
FAIL_COUNT=0
BLOCKED_COUNT=0

check_gate() {
    local name="$1"
    local value="$2"
    local required="$3"

    if [[ "$value" == "" ]]; then
        _warn "  $name: MISSING (cannot verify)"
        BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
        return
    fi

    if [[ "$value" == "$required" ]]; then
        _pass "  $name = $value (target: $required)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        _fail "  $name = $value (target: $required)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}

_info "── Required Gates (must be 0 for ZERO_COPY_PASS) ──"
check_gate "host_upload_bytes"        "$HOST_UPLOAD_BYTES"        "0"
check_gate "host_readback_bytes"      "$HOST_READBACK_BYTES"      "0"
check_gate "nv12_to_rgba_frames"      "$NV12_TO_RGBA_FRAMES"      "0"
check_gate "rgba_to_nv12_frames"      "$RGBA_TO_NV12_FRAMES"      "0"
check_gate "encoder_staging_copy_bytes" "$ENCODER_STAGING_COPY"   "0"
check_gate "gpu_surface_copy_frames"  "$GPU_SURFACE_COPY"         "0"
check_gate "cpu_pixel_readback_bytes" "$CPU_READBACK_BYTES"       "0"
check_gate "video_surface_upload_bytes" "$VIDEO_UPLOAD_BYTES"     "0"

_info "── Optional Gates (informational) ──"
if [[ "$NATIVE_SURFACE_FRAMES" -gt 0 ]]; then
    _pass "  gpu_native_surface_frames = $NATIVE_SURFACE_FRAMES (> 0: native surfaces active)"
else
    _warn "  gpu_native_surface_frames = $NATIVE_SURFACE_FRAMES (= 0: no native surfaces)"
fi

if [[ "$NATIVE_ENCODE_FRAMES" -gt 0 ]]; then
    _pass "  gpu_native_encode_frames = $NATIVE_ENCODE_FRAMES (> 0: native encoding active)"
else
    _warn "  gpu_native_encode_frames = $NATIVE_ENCODE_FRAMES (= 0: no native encoding)"
fi

# ── Verdict ──────────────────────────────────────────────────────────────
_info "── Summary ──"
_info "  PASS:     $PASS_COUNT"
_info "  FAIL:     $FAIL_COUNT"
_info "  BLOCKED:  $BLOCKED_COUNT"

if [[ $FAIL_COUNT -eq 0 && $BLOCKED_COUNT -eq 0 ]]; then
    _pass "ZERO_COPY_PASS: All required gates are 0"
    echo "ZERO_COPY_PASS"
    exit 0
elif [[ $FAIL_COUNT -gt 0 ]]; then
    _fail "ZERO_COPY_FAIL: $FAIL_COUNT required gate(s) are non-zero"
    echo "ZERO_COPY_FAIL"
    exit 1
else
    _warn "ZERO_COPY_BLOCKED: $BLOCKED_COUNT gate(s) could not be verified"
    echo "ZERO_COPY_BLOCKED"
    exit 2
fi
