#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_zero_copy_end_to_end.sh — Zero-copy certification gate
#
# Validates that the video pipeline achieves true zero-copy end-to-end:
#   NVDEC → native surface → CUDA/Vulkan compose → NVENC
#
# 13 MANDATORY gates (Phase 13: no "optional" gates — every gate below must
# pass for ZERO_COPY_PASS; a gate whose value cannot be verified is a BLOCKED
# verdict, never a pass):
#   1.  host_upload_bytes = 0         (no CPU→GPU uploads)
#   2.  host_readback_bytes = 0       (no GPU→CPU readbacks)
#   3.  nv12_to_rgba_frames = 0       (no NV12→RGBA conversions)
#   4.  rgba_to_nv12_frames = 0       (no RGBA→NV12 conversions)
#   5.  encoder_staging_copy_bytes = 0 (no encoder staging copies)
#   6.  gpu_surface_copy_frames = 0   (no GPU surface copies)
#   7.  cpu_pixel_readback_bytes = 0  (no CPU pixel readbacks)
#   8.  video_surface_upload_bytes = 0 (no video surface uploads)
#   9.  gpu_native_surface_frames > 0  (frames using native GPU surfaces)
#   10. gpu_native_encode_frames > 0   (frames encoded natively via NVENC)
#   11. effective_backend identity    (must match the requested backend)
#   12. encoder_backend identity      (must be the native/NVENC authority)
#   13. surface_handoff_path = direct (no indirect surface handoff)
#
# Usage:
#   bash tools/verify_zero_copy_end_to_end.sh
#   bash tools/verify_zero_copy_end_to_end.sh --scene DarkGridBackground --frames 60
#   bash tools/verify_zero_copy_end_to_end.sh --report-json /path/to/report.json
#
# Scene note (2026-09-07, Z0 census): the historical default
# `TextPlaceAnimatedCenter` is no longer registered in this repository (the
# content/ compositions tree was externalized; the CLI registers only
# `DarkGridBackground` and `CameraImageClip` plus dev/content packs). The
# default is now `DarkGridBackground` — a registered built-in procedural
# composition with no external assets — so the certification gate is runnable
# on current main without a content pack. CHRONON3D_BENCH_SCENE still
# overrides it for hosts that provide a content pack.
#
# Exit codes:
#   0 = ZERO_COPY_PASS
#   1 = ZERO_COPY_FAIL (non-zero copy metrics detected)
#   2 = BLOCKED (report missing/unreadable or required fields absent)
# ═══════════════════════════════════════════════════════════════════════════

set -uo pipefail

GATE_NAME="verify_zero_copy_end_to_end"

# ── Defaults ─────────────────────────────────────────────────────────────
SCENE="${CHRONON3D_BENCH_SCENE:-DarkGridBackground}"
FRAMES="${CHRONON3D_BENCH_FRAMES:-60}"
WARMUP="${CHRONON3D_BENCH_WARMUP:-10}"
REPORT_JSON=""
OUTPUT_DIR="/tmp/zero_copy_verify"

CLI_BIN="${CHRONON3D_CLI:-}"
if [[ -z "$CLI_BIN" ]]; then
    for candidate in \
        "$PWD/build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli" \
        "$PWD/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli" \
        "$PWD/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli" \
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
    # Try job.<name> and job.gpu.<name> (the *.timing.json sidecar layout)
    job = data.get('job', {})
    if '$counter_name' in job:
        print(job['$counter_name'])
        sys.exit(0)
    gpu = job.get('gpu', {})
    if '$counter_name' in gpu:
        print(gpu['$counter_name'])
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
        --frames "0-$((FRAMES - 1))" \
        --assets-root "$PWD" \
        --warmup-renderer \
        --warmup-framebuffers "$WARMUP" \
        --backend vulkan \
        --hardware nvenc \
        --encoder-backend native \
        --gpu-hot-path-mode require_gpu_native \
        --report \
        -o "$OUTPUT_DIR/zero_copy.mp4" \
        > "$OUTPUT_DIR/render.log" 2>&1; then
        _warn "Video export failed — see $OUTPUT_DIR/render.log"
        echo "GATE_BLOCKED"
        exit 2
    fi

    if [[ ! -s "$OUTPUT_DIR/zero_copy.mp4" ]]; then
        _fail "Video export completed without a non-empty output artifact"
        echo "GATE_BLOCKED"
        exit 2
    fi

    # The timing sidecar is authoritative; render.log is diagnostic only and
    # does not reliably contain aggregate counters.
    TIMING_JSON="$OUTPUT_DIR/zero_copy.mp4.timing.json"
    if [[ -f "$TIMING_JSON" ]]; then
        cp "$TIMING_JSON" "$REPORT_JSON"
        _info "Using authoritative timing sidecar: $TIMING_JSON"
    fi

    if [[ -f "$REPORT_JSON" ]]; then
        _info "Report found at $REPORT_JSON"
    else
        # Phase 13 (final certification): the timing sidecar is the AUTHORITY.
        # A certification run never falls back to scraping render.log for
        # counters — a missing sidecar is an unverifiable gate, so the verdict
        # is ZERO_COPY_BLOCKED (never a log-derived PASS).
        _warn "Zero-copy certification BLOCKED: timing sidecar $TIMING_JSON missing (no log fallback in certification mode)"
        echo "ZERO_COPY_BLOCKED"
        exit 2
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
# CUDA-lane half of gate 1 (host uploads): bytes pushed host→device by the
# VideoDeviceRuntime image cache (cuMemcpyHtoD). Reported informationally;
# the certified steady-state window is the job's render interval, while
# asset/texture prewarm uploads land in the setup window (two-window
# semantics, Z-wave F5/F7).
CUDA_HOST_UPLOAD_BYTES=$(extract_counter "$REPORT_JSON" "cuda_host_upload_bytes")

# Z7 formal window document. A report without both windows is unverifiable,
# even when the cumulative counter happens to be zero.
WINDOW_STATUS=$(python3 -c "
import json, sys
try:
    with open('$REPORT_JSON') as f: data = json.load(f)
    windows = data.get('summary', {}).get('windows', {})
    pre = windows.get('prewarm', {})
    steady = windows.get('steady_state', {})
    ok = ('cuda_host_upload_bytes' in pre and 'cuda_host_upload_bytes' in steady)
    print('ok' if ok else '')
except Exception:
    print('')
" 2>/dev/null)

EFFECTIVE_BACKEND=$(extract_counter "$REPORT_JSON" "effective_backend")
ENCODER_BACKEND=$(extract_counter "$REPORT_JSON" "encoder_backend")
SURFACE_HANDOFF_PATH=$(extract_counter "$REPORT_JSON" "surface_handoff_path")

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

_info "── Required Native Zero-Copy Identity Gates ──"
check_gate "effective_backend" "$EFFECTIVE_BACKEND" "vulkan"
check_gate "encoder_backend" "$ENCODER_BACKEND" "nvenc"
check_gate "surface_handoff_path" "$SURFACE_HANDOFF_PATH" "direct"
if [[ "$WINDOW_STATUS" == "ok" ]]; then
    _pass "z7_upload_windows = present (prewarm + steady_state)"
    PASS_COUNT=$((PASS_COUNT + 1))
else
    _warn "z7_upload_windows: MISSING (cannot verify prewarm/steady-state semantics)"
    BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
fi

check_positive_gate() {
    local name="$1"
    local value="$2"
    if [[ "$value" == "" ]]; then
        _warn "  $name: MISSING (cannot verify)"
        BLOCKED_COUNT=$((BLOCKED_COUNT + 1))
    elif ! [[ "$value" =~ ^[0-9]+$ ]]; then
        _fail "  $name = $value (not a non-negative integer)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    elif [[ "$value" -gt 0 ]]; then
        _pass "  $name = $value (> 0)"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        _fail "  $name = $value (must be > 0)"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
}
check_positive_gate "gpu_native_surface_frames" "$NATIVE_SURFACE_FRAMES"
check_positive_gate "gpu_native_encode_frames" "$NATIVE_ENCODE_FRAMES"

# ── Verdict ──────────────────────────────────────────────────────────────
_info "── Summary ──"
if [[ "$CUDA_HOST_UPLOAD_BYTES" != "" ]]; then
    _info "  cuda_host_upload_bytes = $CUDA_HOST_UPLOAD_BYTES (CUDA-lane cumulative total; see summary.windows)"
fi
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
