#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/measure_gpu_encoder_phases.sh
#
# Measure the GPU → encoder critical path on the Vulkan + native encoder
# pipeline, WITHOUT implementing zero-copy.  It produces the numbers needed
# to decide whether an NV12 / zero-copy / pinned-staging study is justified:
#
#   gpu_execute_ms   GPU-elapsed time (Vulkan timestamp queries)
#   readback_ms      CPU-side map/memcpy/unmap of the surface download
#   conversion_ms    RGBA → YUV conversion (native encoder, summed per frame)
#   native_send_ms   avcodec_send_frame CPU time
#   backpressure_ms  EAGAIN drain+retry wait
#   receive_ms       avcodec_receive_packet CPU time
#   mux_ms           av_interleaved_write_frame / mux write
#
# All seven come from the single Chronon timing sidecar (<output>.timing.json)
# — the same document RenderingGen ingests as its source of truth — so the
# numbers here are the exact ones the worker would record.
#
# Usage:
#   bash tools/measure_gpu_encoder_phases.sh <composition>
#   bash tools/measure_gpu_encoder_phases.sh <composition> --frames 0-149 --fps 30
#
# Environment:
#   CHRONON3D_MEASURE_CLI_BIN=<path>   Override CLI binary path
#   CHRONON3D_MEASURE_FRAMES=<0-N>     Frame range (default: 0-149 ≈ 5 s @ 30 fps)
#   CHRONON3D_MEASURE_FPS=<N>          Output fps (default: 30)
#   CHRONON3D_MEASURE_BACKEND=<name>   Render backend (default: vulkan)
#   CHRONON3D_MEASURE_KEEP=1           Keep the render workspace + sidecar
#
# Exit: 0 = measured, 1 = render/parse failed, 2 = blocked (no CLI / no comp).
# ═══════════════════════════════════════════════════════════════════════════

set -uo pipefail

GATE_NAME="measure_gpu_encoder_phases"

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)"
if [ -z "$ROOT" ]; then
    ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi
cd "$ROOT"

COMP_ID="${CHRONON3D_MEASURE_COMP_ID:-${1:-}}"
FRAMES="${CHRONON3D_MEASURE_FRAMES:-0-149}"
FPS="${CHRONON3D_MEASURE_FPS:-30}"
BACKEND="${CHRONON3D_MEASURE_BACKEND:-vulkan}"
CLI_BIN_OVERRIDE="${CHRONON3D_MEASURE_CLI_BIN:-}"
KEEP="${CHRONON3D_MEASURE_KEEP:-0}"

# ── Helpers ────────────────────────────────────────────────────────────────

find_chronon3d_cli() {
    if [ -n "$CLI_BIN_OVERRIDE" ] && [ -x "$CLI_BIN_OVERRIDE" ]; then
        echo "$CLI_BIN_OVERRIDE"; return 0
    fi
    for base in \
        "${ROOT}/.tmp/chronon-builds/native-verify" \
        "${ROOT}/build/chronon/linux-content-dev" \
        "${ROOT}/build/chronon/linux-ci" \
        "${ROOT}/build/chronon/linux-fast-dev" \
        "${ROOT}/build/manual-test"; do
        for candidate in "${base}/chronon3d_cli" "${base}/apps/chronon3d_cli/chronon3d_cli"; do
            if [ -n "$candidate" ] && [ -x "$candidate" ]; then
                echo "$candidate"; return 0
            fi
        done
    done
    command -v chronon3d_cli 2>/dev/null && return 0
    return 1
}

usage() {
    echo "Usage: bash tools/measure_gpu_encoder_phases.sh <composition> [--frames 0-N] [--fps N]"
    echo ""
    echo "Env: CHRONON3D_MEASURE_CLI_BIN, CHRONON3D_MEASURE_FRAMES (0-149),"
    echo "     CHRONON3D_MEASURE_FPS (30), CHRONON3D_MEASURE_BACKEND (vulkan),"
    echo "     CHRONON3D_MEASURE_KEEP (1 to keep the workspace + sidecar)"
}

# ── Arg parse (simple: only --frames / --fps / --help are accepted) ────────
while [ $# -gt 0 ]; do
    case "$1" in
        --frames) FRAMES="${2:?--frames requires a value}"; shift 2 ;;
        --fps)    FPS="${2:?--fps requires a value}"; shift 2 ;;
        --backend) BACKEND="${2:?--backend requires a value}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) COMP_ID="$1"; shift ;;
    esac
done

if [ -z "$COMP_ID" ]; then
    echo "[BLOCKED] ${GATE_NAME}: no composition given" >&2
    usage
    exit 2
fi

CLI_BIN="$(find_chronon3d_cli)" || {
    echo "[BLOCKED] ${GATE_NAME}: chronon3d_cli not found — set CHRONON3D_MEASURE_CLI_BIN or build" >&2
    exit 2
}

# ═══════════════════════════════════════════════════════════════════════════
# 1. Render (Vulkan + native encoder)
# ═══════════════════════════════════════════════════════════════════════════
WORK_DIR="$(mktemp -d /tmp/chronon3d_gpu_measure.XXXXXX)"
OUT_MP4="${WORK_DIR}/result.mp4"
SIDECAR="${OUT_MP4}.timing.json"

echo "== 1. Render (backend=${BACKEND}, encoder=native, pipe) =="
echo "   composition: ${COMP_ID}  frames: ${FRAMES}  fps: ${FPS}"
echo "   binary:      ${CLI_BIN}"

# The sidecar + report land in the CLI's CWD; run from the work dir so they
# are captured predictably regardless of where the script is invoked.
RENDER_LOG="${WORK_DIR}/render.log"
(
    cd "$WORK_DIR" || exit 1
    "$CLI_BIN" render "$COMP_ID" \
        --frames "$FRAMES" \
        --fps "$FPS" \
        --backend "$BACKEND" \
        --encoder-backend native \
        --ffmpeg-mode pipe \
        --report \
        -o "$OUT_MP4"
) > "$RENDER_LOG" 2>&1
RC=$?
if [ "$RC" -ne 0 ]; then
    echo "[FAIL] ${GATE_NAME}: render exited $RC" >&2
    if grep -qE "native not in|encoder-backend" "$RENDER_LOG"; then
        echo "       this build lacks the native encoder (CHRONON3D_ENABLE_NATIVE_FFMPEG=OFF)." >&2
        echo "       point CHRONON3D_MEASURE_CLI_BIN at a native-FFMPEG build." >&2
    elif grep -qiE "vulkan|gpu" "$RENDER_LOG"; then
        echo "       vulkan is strict — verify the GPU backend is available." >&2
    fi
    tail -n 5 "$RENDER_LOG" >&2
    rm -rf "$WORK_DIR"
    exit 1
fi

if [ ! -f "$SIDECAR" ]; then
    echo "[FAIL] ${GATE_NAME}: timing sidecar missing at ${SIDECAR}" >&2
    rm -rf "$WORK_DIR"
    exit 1
fi

# ═══════════════════════════════════════════════════════════════════════════
# 2. Parse the sidecar and print the 7-phase breakdown + verdict
# ═══════════════════════════════════════════════════════════════════════════
SIDECAR="$SIDECAR" python3 - <<'PY'
import json, os, sys

path = os.environ["SIDECAR"]
with open(path) as fh:
    doc = json.load(fh)

job = doc.get("job") or {}
enc = job.get("encoder") or {}
gpu = job.get("gpu") or {}

def num(v):
    return v if isinstance(v, (int, float)) else 0.0

# Native conversion is reported per-frame; sum it for the job-level total.
conversion = 0.0
for frame in doc.get("frame_times_ms", []) or []:
    conversion += num(frame.get("native_convert_ms"))

gpu_execute  = num(gpu.get("gpu_execute_ms"))
readback     = num(gpu.get("gpu_readback_ms"))
send         = num(enc.get("submit_cpu_ms"))
backpressure = num(enc.get("backpressure_wait_ms"))
receive      = num(enc.get("packet_receive_ms"))
mux          = num(enc.get("mux_packet_ms"))

wall_ms      = num(doc.get("wall_time_ms"))
frames       = num(doc.get("frames_total"))
summary      = doc.get("summary") or {}

print("")
print("== 2. GPU -> encoder phase breakdown (ms, job totals) ==")
rows = [
    ("gpu_execute",      gpu_execute,  "(Vulkan timestamp queries, GPU-elapsed)"),
    ("readback",         readback,     "(CPU map/memcpy/unmap of surface download)"),
    ("conversion",       conversion,   "(RGBA -> YUV, native encoder, summed/frame)"),
    ("native_send",      send,         "(avcodec_send_frame)"),
    ("backpressure",     backpressure, "(EAGAIN drain+retry wait)"),
    ("receive",          receive,      "(avcodec_receive_packet)"),
    ("mux",              mux,          "(av_interleaved_write_frame)"),
]
print(f"{'phase':<14} {'ms':>12}   note")
for name, value, note in rows:
    print(f"{name:<14} {value:>12.3f}   {note}")

print("")
print(f"context: wall_time={wall_ms:.1f} ms  frames={int(frames)}  "
      f"e2e_fps={num(summary.get('end_to_end_fps')):.2f}")
if gpu:
    print(f"gpu:     readback_bytes={gpu.get('gpu_readback_bytes')}  "
          f"upload_bytes={gpu.get('gpu_upload_bytes')}  "
          f"submissions={gpu.get('gpu_submissions')}  "
          f"passes={gpu.get('passes_executed')}  "
          f"submit_cpu={num(gpu.get('gpu_submit_cpu_ms')):.3f} ms  "
          f"wait_cpu={num(gpu.get('gpu_wait_cpu_ms')):.3f} ms")
else:
    print("gpu:     (null — software backend; no GPU counters exported)")

# ── Verdict: is readback + conversion the dominant cost? ──────────────────
readback_plus_conversion = readback + conversion
encoder_sum = send + backpressure + receive + mux

print("")
print("== 3. Verdict (zero-copy justification) ==")
if wall_ms > 0:
    rc_share = readback_plus_conversion / wall_ms * 100.0
    print(f"readback + conversion = {readback_plus_conversion:.3f} ms "
          f"({rc_share:.1f}% of wall)")
    print(f"gpu_execute           = {gpu_execute:.3f} ms "
          f"({(gpu_execute / wall_ms * 100.0):.1f}% of wall)")
    print(f"encoder (send+bp+recv+mux) = {encoder_sum:.3f} ms "
          f"({(encoder_sum / wall_ms * 100.0):.1f}% of wall)")

    if readback_plus_conversion > 0 and readback_plus_conversion >= gpu_execute \
            and rc_share >= 20.0:
        print("")
        print("=> readback + conversion dominate. An NV12 / zero-copy / pinned-staging")
        print("   study IS justified by these numbers.")
    elif readback_plus_conversion > 0:
        print("")
        print("=> readback + conversion are NOT the dominant cost yet.")
        print("   Zero-copy is NOT justified by these numbers; look at gpu_execute or")
        print("   the encoder phases first.")
    else:
        print("")
        print("=> readback/conversion unmeasured (software backend or 0 values);")
        print("   re-run with --backend vulkan to get GPU numbers.")
else:
    print("wall_time_ms is 0 — cannot compute shares.")
PY

RC=$?
if [ "$RC" -ne 0 ]; then
    echo "[FAIL] ${GATE_NAME}: sidecar parse failed" >&2
    rm -rf "$WORK_DIR"
    exit 1
fi

# ═══════════════════════════════════════════════════════════════════════════
# 3. Cleanup
# ═══════════════════════════════════════════════════════════════════════════
echo ""
if [ "$KEEP" = "1" ]; then
    echo "[INFO] ${GATE_NAME}: kept workspace + sidecar at ${WORK_DIR}"
else
    rm -rf "$WORK_DIR"
    echo "[INFO] ${GATE_NAME}: cleaned workspace (set CHRONON3D_MEASURE_KEEP=1 to keep ${SIDECAR})"
fi
exit 0
