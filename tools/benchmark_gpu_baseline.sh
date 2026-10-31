#!/usr/bin/env bash
# Official reproducible GPU baseline runner. The timing sidecar is the
# authority; render logs are retained only as diagnostics.
#
# Example:
#   bash tools/benchmark_gpu_baseline.sh \
#     --case GPU_TEXT_01=Clean --case GPU_TEXT_02=Neon
#
# Content packs may provide the typewriter compositions. Without --case the
# built-in DarkGridBackground case keeps the harness runnable on clean main.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLI_BIN="${CHRONON3D_CLI:-}"
OUTPUT_DIR="${CHRONON3D_BASELINE_DIR:-$REPO_ROOT/artifacts/gpu-baseline}"
CORPUS_FILE=""
FRAMES="${CHRONON3D_BENCH_FRAMES:-120}"
WARMUP_FRAMEBUFFERS="${CHRONON3D_BENCH_WARMUP_FRAMEBUFFERS:-10}"
BACKEND="${CHRONON3D_BENCH_BACKEND:-vulkan}"
HARDWARE="${CHRONON3D_BENCH_HARDWARE:-nvenc}"
ENCODER_BACKEND="${CHRONON3D_BENCH_ENCODER_BACKEND:-native}"
GPU_HOT_PATH="${CHRONON3D_BENCH_GPU_HOT_PATH:-auto}"
ENCODE_PRESET="${CHRONON3D_BENCH_ENCODE_PRESET:-}"
ASSETS_ROOT="${CHRONON3D_BENCH_ASSETS_ROOT:-$REPO_ROOT}"
CONTENT_PACK_ID="${CHRONON3D_CONTENT_PACK_ID:-}"
CASES=()

usage() {
    sed -n '2,14p' "$0"
    cat >&2 <<'EOF'

Options:
  --cli PATH                 chronon3d_cli binary
  --out DIR                  output/report directory
  --corpus FILE              versioned JSON corpus manifest
  --case ID=SCENE            case to run (repeatable)
  --frames N                 rendered frames (default: 120)
  --warmup N                 framebuffers to prewarm (default: 10)
  --backend NAME             auto, software, or vulkan
  --hardware NAME            encoder hardware (default: nvenc)
  --encoder-backend NAME     pipe or native (default: native)
  --gpu-hot-path-mode NAME   auto or require_gpu_native
  --encode-preset NAME       encoder preset, e.g. p1 (default: encoder default)
  --assets-root DIR          asset root passed to render

Certification requires CHRONON3D_CONTENT_PACK_ID to identify the exact pack
revision/hash. Set CHRONON3D_ALLOW_DIRTY=1 only for explicitly non-certifying
local diagnostics. The report captures the source commit and binary SHA-256.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cli)                 CLI_BIN="$2"; shift 2 ;;
        --out)                 OUTPUT_DIR="$2"; shift 2 ;;
        --corpus)              CORPUS_FILE="$2"; shift 2 ;;
        --case)                CASES+=("$2"); shift 2 ;;
        --frames)              FRAMES="$2"; shift 2 ;;
        --warmup)              WARMUP_FRAMEBUFFERS="$2"; shift 2 ;;
        --backend)             BACKEND="$2"; shift 2 ;;
        --hardware)            HARDWARE="$2"; shift 2 ;;
        --encoder-backend)     ENCODER_BACKEND="$2"; shift 2 ;;
        --gpu-hot-path-mode)   GPU_HOT_PATH="$2"; shift 2 ;;
        --encode-preset)       ENCODE_PRESET="$2"; shift 2 ;;
        --assets-root)          ASSETS_ROOT="$2"; shift 2 ;;
        --content-pack-id)      CONTENT_PACK_ID="$2"; shift 2 ;;
        -h|--help)             usage; exit 0 ;;
        *) echo "[ERROR] unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$CLI_BIN" ]]; then
    for candidate in \
        "$REPO_ROOT/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli" \
        "$REPO_ROOT/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli" \
        "$REPO_ROOT/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli"; do
        if [[ -x "$candidate" ]]; then CLI_BIN="$candidate"; break; fi
    done
fi
[[ -x "$CLI_BIN" ]] || { echo "[ERROR] chronon3d_cli not found: $CLI_BIN" >&2; exit 2; }
[[ "$FRAMES" =~ ^[1-9][0-9]*$ ]] || { echo "[ERROR] --frames must be positive" >&2; exit 2; }
[[ -n "$CONTENT_PACK_ID" ]] || { echo "[ERROR] --content-pack-id (or CHRONON3D_CONTENT_PACK_ID) is required" >&2; exit 2; }
if [[ "${CHRONON3D_ALLOW_DIRTY:-0}" != "1" ]] && ! git -C "$REPO_ROOT" diff --quiet; then
    echo "[ERROR] tracked worktree is dirty; certify from a clean checkout or set CHRONON3D_ALLOW_DIRTY=1 for diagnostics only" >&2
    exit 2
fi
if [[ "${CHRONON3D_ALLOW_DIRTY:-0}" != "1" ]] && [[ -n "$(git -C "$REPO_ROOT" ls-files --others --exclude-standard)" ]]; then
    echo "[ERROR] untracked files are present; certify from a clean checkout or set CHRONON3D_ALLOW_DIRTY=1 for diagnostics only" >&2
    exit 2
fi
if [[ "$BACKEND" != "vulkan" || "$HARDWARE" != "nvenc" || "$ENCODER_BACKEND" != "native" || "$GPU_HOT_PATH" != "require_gpu_native" ]]; then
    echo "[ERROR] official GPU corpus requires --backend vulkan --hardware nvenc --encoder-backend native --gpu-hot-path-mode require_gpu_native" >&2
    exit 2
fi
[[ "$WARMUP_FRAMEBUFFERS" =~ ^[0-9]+$ ]] || { echo "[ERROR] --warmup must be non-negative" >&2; exit 2; }
if [[ -n "$CORPUS_FILE" && ${#CASES[@]} -gt 0 ]]; then
    echo "[ERROR] --corpus and --case are mutually exclusive" >&2
    exit 2
fi
if [[ -n "$CORPUS_FILE" && ! -f "$CORPUS_FILE" ]]; then
    echo "[ERROR] corpus manifest not found: $CORPUS_FILE" >&2
    exit 2
fi
if [[ -z "$CORPUS_FILE" && ${#CASES[@]} -eq 0 ]]; then CASES+=("B00=DarkGridBackground"); fi

mkdir -p "$OUTPUT_DIR"
REPORT_JSON="$OUTPUT_DIR/gpu-baseline.json"
export CLI_BIN OUTPUT_DIR CORPUS_FILE FRAMES WARMUP_FRAMEBUFFERS BACKEND HARDWARE ENCODER_BACKEND GPU_HOT_PATH ENCODE_PRESET ASSETS_ROOT CONTENT_PACK_ID
export REPO_ROOT

python3 - "$REPORT_JSON" "${CASES[@]}" <<'PY'
import json
import os
import pathlib
import re
import subprocess
import sys
import time
import hashlib
import math
import platform
from fractions import Fraction

report_path = pathlib.Path(sys.argv[1])
cases = sys.argv[2:]
cli = os.environ["CLI_BIN"]
out_root = pathlib.Path(os.environ["OUTPUT_DIR"])
corpus_file = os.environ.get("CORPUS_FILE", "")
frames = int(os.environ["FRAMES"])
warmup_framebuffers = int(os.environ["WARMUP_FRAMEBUFFERS"])
backend = os.environ["BACKEND"]
hardware = os.environ["HARDWARE"]
encoder_backend = os.environ["ENCODER_BACKEND"]
gpu_hot_path = os.environ["GPU_HOT_PATH"]
encode_preset = os.environ.get("ENCODE_PRESET", "")
assets_root = os.environ["ASSETS_ROOT"]
content_pack_id = os.environ["CONTENT_PACK_ID"]

manifest = None
acceptance = {
    "minimum_wall_attribution_percent": 95.0,
    "full_file_reread_count": 0,
    "require_direct_surface_handoff": True,
    "require_execution_downgrade_counter": True,
    "require_zero_software_encode_frames": True,
    "require_zero_video_pipe_fallback_frames": True,
    "require_encoded_frame_count_match": True,
}
if corpus_file:
    try:
        manifest = json.loads(pathlib.Path(corpus_file).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid corpus manifest: {exc}")
    if not isinstance(manifest, dict):
        raise SystemExit("invalid corpus manifest: top-level value must be an object")
    manifest_cases = manifest.get("cases")
    if not isinstance(manifest_cases, list) or len(manifest_cases) != 17:
        raise SystemExit("invalid corpus manifest: exactly 17 canonical cases are required")
    if manifest.get("id") != "chronon3d-gpu-corpus-v1" or manifest.get("version") != "1.2":
        raise SystemExit("unsupported GPU corpus identity/version")
    acceptance = manifest.get("acceptance")
    if acceptance != {
        "minimum_wall_attribution_percent": 95.0,
        "full_file_reread_count": 0,
        "require_direct_surface_handoff": True,
        "require_execution_downgrade_counter": True,
        "require_zero_software_encode_frames": True,
        "require_zero_video_pipe_fallback_frames": True,
        "require_encoded_frame_count_match": True,
    }:
        raise SystemExit("GPU corpus acceptance contract missing or unsupported")
    defaults = manifest.get("defaults", {})
    if not isinstance(defaults, dict):
        raise SystemExit("invalid corpus manifest: defaults must be an object")
    if defaults.get("backend") != "vulkan" or defaults.get("hardware") != "nvenc" or defaults.get("encoder_backend") != "native" or defaults.get("gpu_hot_path_mode") != "require_gpu_native":
        raise SystemExit("GPU corpus defaults must require strict Vulkan/native NVENC")
    if backend != defaults["backend"] or hardware != defaults["hardware"] or encoder_backend != defaults["encoder_backend"] or gpu_hot_path != defaults["gpu_hot_path_mode"]:
        raise SystemExit("runner options disagree with strict GPU corpus defaults")
    if int(os.environ["FRAMES"]) != defaults["frames"] or int(os.environ["WARMUP_FRAMEBUFFERS"]) != defaults["warmup_framebuffers"]:
        raise SystemExit("runner --frames/--warmup cannot override corpus-wide parameters")
    cases = []
    seen_ids = set()
    expected_ids = [*(f"B{i:02d}" for i in range(12)),
                    *(f"GPU_TEXT_{i:02d}" for i in range(1, 6))]
    manifest_ids = [item.get("id") if isinstance(item, dict) else None
                    for item in manifest_cases]
    if manifest_ids != expected_ids:
        raise SystemExit("GPU corpus manifest case ids/order do not match the canonical B00-B11 + GPU_TEXT_01-05 sequence")
    for item in manifest_cases:
        if not isinstance(item, dict):
            raise SystemExit("invalid corpus manifest: each case must be an object")
        case_id = item.get("id")
        scene = item.get("scene")
        category = item.get("category")
        if not isinstance(case_id, str) or not case_id or not isinstance(scene, str) or not scene or not isinstance(category, str) or not category:
            raise SystemExit("invalid corpus manifest: each case needs non-empty id, scene, and category")
        if case_id in seen_ids:
            raise SystemExit(f"invalid corpus manifest: duplicate case id {case_id!r}")
        seen_ids.add(case_id)
        case_frames = item.get("frames", defaults.get("frames", frames))
        case_fps = item.get("fps", defaults.get("fps", 30))
        if not isinstance(case_frames, int) or isinstance(case_frames, bool) or case_frames <= 0:
            raise SystemExit(f"invalid corpus manifest: {case_id}.frames must be a positive integer")
        if not isinstance(case_fps, int) or isinstance(case_fps, bool) or not 1 <= case_fps <= 240:
            raise SystemExit(f"invalid corpus manifest: {case_id}.fps must be in 1..240")
        case_warmup = item.get("warmup_framebuffers", defaults.get("warmup_framebuffers", warmup_framebuffers))
        fragmented_mp4 = item.get("fragmented_mp4", defaults.get("fragmented_mp4", True))
        if not isinstance(case_warmup, int) or isinstance(case_warmup, bool) or case_warmup < 0:
            raise SystemExit(f"invalid corpus manifest: {case_id}.warmup_framebuffers must be a non-negative integer")
        if not isinstance(fragmented_mp4, bool):
            raise SystemExit(f"invalid corpus manifest: {case_id}.fragmented_mp4 must be boolean")
        cases.append({"id": case_id, "scene": scene, "category": category,
                      "frames": case_frames, "fps": case_fps,
                      "warmup_framebuffers": case_warmup,
                      "fragmented_mp4": fragmented_mp4})

def safe_name(value):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("._") or "case"

def get(data, *path):
    for key in path:
        if not isinstance(data, dict): return None
        data = data.get(key)
    return data

def numeric(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)

def phase(data, name):
    aliases = {
        "startup_ms": [("startup_breakdown", "total_startup_ms"), ("exclusive_wall_timeline", "startup_ms")],
        "prepare_ms": [("job", "prepare_ms"), ("exclusive_wall_timeline", "prepare_ms")],
        "render_loop_ms": [("job", "render_loop_wall_ms"), ("exclusive_wall_timeline", "render_loop_ms")],
        "encode_drain_ms": [("job", "encoder_finalize_ms"), ("exclusive_wall_timeline", "encoder_drain_finalize_ms")],
        "finalize_ms": [("job", "output_finalize_ms"), ("exclusive_wall_timeline", "output_finalize_ms")],
        "hashing_ms": [("job", "sha256_ms"), ("exclusive_wall_timeline", "sha256_ms")],
    }
    for candidate in aliases[name]:
        value = get(data, *candidate)
        if numeric(value): return float(value)
    return None

try:
    commit = subprocess.check_output(["git", "-C", os.environ["REPO_ROOT"], "rev-parse", "HEAD"], text=True).strip()
    source_dirty = bool(subprocess.check_output(["git", "-C", os.environ["REPO_ROOT"], "status", "--porcelain"], text=True).strip())
except (OSError, subprocess.CalledProcessError):
    commit, source_dirty = "unknown", True
try:
    binary_sha256 = hashlib.sha256(pathlib.Path(cli).resolve().read_bytes()).hexdigest()
except OSError as exc:
    raise SystemExit(f"cannot hash CLI binary {cli!r}: {exc}")
manifest_sha256 = hashlib.sha256(pathlib.Path(corpus_file).read_bytes()).hexdigest() if corpus_file else None
try:
    gpu = subprocess.check_output(["nvidia-smi", "--query-gpu=name,driver_version,uuid,memory.total", "--format=csv,noheader"], text=True).strip().splitlines()
except (OSError, subprocess.CalledProcessError):
    gpu = []
try:
    native_library_versions = subprocess.check_output(["ffmpeg", "-hide_banner", "-version"], text=True, stderr=subprocess.STDOUT).splitlines()[:2]
except (OSError, subprocess.CalledProcessError):
    native_library_versions = []

result = {
    "schema_version": 3,
    "commit": commit,
    "source_dirty": source_dirty,
    "binary": str(pathlib.Path(cli).resolve()),
    "binary_sha256": binary_sha256,
    "manifest_sha256": manifest_sha256,
    "content_pack_id": content_pack_id,
    "host": {"platform": platform.platform(), "python": platform.python_version(), "gpus": gpu,
             "ffmpeg_build": native_library_versions},
    "corpus": manifest.get("id") if manifest else None,
    "requested_cases": cases,
    "manifest_case_ids": [item["id"] for item in cases] if manifest else [],
    "parameters": {"frames": frames, "warmup_framebuffers": warmup_framebuffers, "backend": backend,
                   "hardware": hardware, "encoder_backend": encoder_backend,
                   "gpu_hot_path_mode": gpu_hot_path,
                   "encode_preset": encode_preset},
    "cases": [],
}

if manifest:
    list_result = subprocess.run([cli, "list"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if list_result.returncode != 0:
        raise SystemExit(f"could not enumerate registered compositions: {list_result.stdout[-2000:]}")
    registered_scenes = {line.split()[0] for line in list_result.stdout.splitlines()
                     if line.split() and not line.lstrip().startswith("-")
                     and line.split()[0] not in ("ID", "OPTIONS:")}
    missing_scenes = sorted({item["scene"] for item in cases if item["scene"] not in registered_scenes})
    if missing_scenes:
        for item in cases:
            missing = item["scene"] in missing_scenes
            result["cases"].append({
                "id": item["id"], "scene": item["scene"],
                "expected_frames": item["frames"], "fps": item["fps"],
                "exit_code": None,
                "error_class": "composition_not_registered" if missing else "not_run_due_to_missing_composition",
                "error": "scene is not registered by the active content pack" if missing
                         else "case not run because the corpus preflight found missing scenes",
                "output": None, "timing_sidecar": None,
            })
        result["case_gate_pass"] = False
        result["certification_eligible"] = False
        result["certification_blockers"] = ["content pack does not register every canonical corpus scene"]
        report_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        print(json.dumps({"report": str(report_path), "cases": len(result["cases"]),
                          "failed": len(missing_scenes), "case_gate_pass": False,
                          "certification_eligible": False}))
        raise SystemExit(1)

for case in cases:
    if isinstance(case, str):
        if "=" not in case: raise SystemExit(f"invalid --case {case!r}; expected ID=SCENE")
        case_id, scene = case.split("=", 1)
        expected_frames, case_fps = frames, 30
        case_warmup, fragmented_mp4 = warmup_framebuffers, True
    else:
        case_id, scene = case["id"], case["scene"]
        expected_frames, case_fps = case["frames"], case["fps"]
        case_warmup, fragmented_mp4 = case["warmup_framebuffers"], case["fragmented_mp4"]
    if not case_id or not scene: raise SystemExit(f"invalid case {case!r}")
    case_dir = out_root / safe_name(case_id)
    case_dir.mkdir(parents=True, exist_ok=True)
    output = case_dir / "render.mp4"
    log = case_dir / "render.log"
    command = [cli, "render", scene, "--frames", f"0-{expected_frames - 1}",
               "--fps", str(case_fps), "--assets-root", assets_root, "--warmup-renderer",
               "--warmup-framebuffers", str(case_warmup),
               "--backend", backend, "--hardware", hardware,
               "--encoder-backend", encoder_backend,
               "--gpu-hot-path-mode", gpu_hot_path, "--report", "-o", str(output)]
    if fragmented_mp4:
        command.append("--fragmented-mp4")
    if encode_preset:
        command[command.index("--report"):command.index("--report")] = [
            "--encode-preset", encode_preset]
    started = time.monotonic()
    with log.open("w") as stream:
        completed = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    process_wall_ms = (time.monotonic() - started) * 1000.0
    sidecar_path = pathlib.Path(str(output) + ".timing.json")
    entry = {"id": case_id, "scene": scene, "exit_code": completed.returncode,
             "output": str(output), "timing_sidecar": str(sidecar_path),
             "command": command, "expected_frames": expected_frames, "fps": case_fps,
             "process_startup_ms": None, "prepare_ms": None,
             "warmup_ms": None, "steady_state_ms": None,
             "encode_drain_ms": None, "finalize_ms": None,
             "full_file_reread_count": None,
             "total_wall_ms": process_wall_ms, "attribution": None,
             "execution_path": None, "frames": None,
             "error": None if completed.returncode == 0 else f"render command exited {completed.returncode}"}
    if not sidecar_path.is_file():
        # A missing sidecar after a non-zero render is a command-level
        # failure, not a timing-contract failure. Keep that distinction in
        # the corpus report so an absent external content pack is not
        # misdiagnosed as a wall-time instrumentation defect.
        if completed.returncode != 0:
            entry["error"] = (
                f"render command failed before timing sidecar "
                f"(exit_code={completed.returncode}); inspect render.log"
            )
            entry["error_class"] = "command_failed_before_sidecar"
        else:
            entry["error"] = "timing sidecar missing after successful render"
            entry["error_class"] = "timing_sidecar_missing"
    else:
        try:
            data = json.loads(sidecar_path.read_text())
            summary = data.get("summary", {})
            job = data.get("job", {})
            frame_times = data.get("frame_times_ms")
            reported_frames = data.get("frames_total")
            entry["frame_times_count"] = len(frame_times) if isinstance(frame_times, list) else None
            entry["reported_frames_total"] = reported_frames
            entry["frames"] = entry["frame_times_count"]
            entry["execution_path"] = job.get("execution_path")
            entry["process_startup_ms"] = phase(data, "startup_ms")
            entry["prepare_ms"] = phase(data, "prepare_ms")
            entry["warmup_ms"] = summary.get("warmup_avg_ms")
            if entry["warmup_ms"] is None and summary.get("warmup_frames", 0) == 0:
                entry["warmup_ms"] = 0.0
            entry["steady_state_ms"] = summary.get("steady_avg_ms")
            entry["encode_drain_ms"] = phase(data, "encode_drain_ms")
            entry["finalize_ms"] = phase(data, "finalize_ms")
            entry["attribution"] = data.get("phase_attribution") or data.get("exclusive_wall_timeline", {})
            entry["full_file_reread_count"] = get(data, "job", "full_file_reread_count")
            entry["windows"] = summary.get("windows", {})
            entry["prewarm_prepare_ms"] = get(data, "prepare_breakdown", "total_prepare_ms")
            accounted = get(data, "exclusive_wall_timeline", "accounted_percent")
            entry["effective_backend"] = get(data, "job", "gpu", "effective_backend")
            entry["surface_handoff_path"] = get(data, "job", "surface_handoff_path")
            if not numeric(accounted):
                entry["error"] = entry["error"] or "wall-time attribution missing or non-finite accounted_percent"
            elif float(accounted) < acceptance["minimum_wall_attribution_percent"]:
                entry["error"] = f"wall-time attribution below {acceptance['minimum_wall_attribution_percent']:.2f}%: {accounted:.2f}%"
            elif not isinstance(entry["windows"], dict) or not all(
                isinstance(entry["windows"].get(name), dict)
                for name in ("prewarm", "steady_state")
            ):
                entry["error"] = "prewarm/steady_state windows missing"
            elif not isinstance(entry["windows"]["prewarm"].get("frame_count"), int) or isinstance(entry["windows"]["prewarm"].get("frame_count"), bool):
                entry["error"] = "prewarm.frame_count missing or invalid"
            elif not isinstance(entry["windows"]["steady_state"].get("frame_count"), int) or isinstance(entry["windows"]["steady_state"].get("frame_count"), bool):
                entry["error"] = "steady_state.frame_count missing or invalid"
            elif (entry["windows"]["prewarm"]["frame_count"] < 0
                  or entry["windows"]["steady_state"]["frame_count"] < 0
                  or entry["windows"]["prewarm"]["frame_count"] + entry["windows"]["steady_state"]["frame_count"] != expected_frames):
                window_frames = (entry["windows"]["prewarm"]["frame_count"]
                                 + entry["windows"]["steady_state"]["frame_count"])
                entry["error"] = f"prewarm/steady_state frame count mismatch: expected {expected_frames}, got {window_frames}"
            elif any(not numeric(entry[name]) for name in (
                "process_startup_ms", "prepare_ms", "warmup_ms",
                "steady_state_ms", "encode_drain_ms", "finalize_ms")):
                entry["error"] = "required timing bucket missing or non-numeric"
            if not isinstance(entry["full_file_reread_count"], int) or isinstance(entry["full_file_reread_count"], bool):
                entry["error"] = "job.full_file_reread_count missing or invalid"
            elif entry["full_file_reread_count"] != acceptance["full_file_reread_count"]:
                entry["error"] = f"job.full_file_reread_count must be exactly {acceptance['full_file_reread_count']}, got {entry['full_file_reread_count']!r}"
            if (not isinstance(entry["reported_frames_total"], int)
                    or isinstance(entry["reported_frames_total"], bool)
                    or entry["frames"] != expected_frames
                    or entry["reported_frames_total"] != expected_frames):
                entry["error"] = (f"frame count mismatch: expected {expected_frames}, "
                                   f"frame_times={entry['frame_times_count']}, "
                                   f"frames_total={entry['reported_frames_total']}")
            if job.get("execution_path") != "full_graph_native":
                entry["error"] = f"execution path must be full_graph_native, got {job.get('execution_path')!r}"
            downgrade_count = job.get("execution_downgrade_count")
            entry["execution_downgrade_count"] = downgrade_count
            if not isinstance(downgrade_count, int) or isinstance(downgrade_count, bool) or downgrade_count < 0:
                entry["error"] = "execution_downgrade_count missing or invalid"
            if encoder_backend == "native" and entry["frames"] == expected_frames:
                gpu = get(data, "job", "gpu") or {}
                for counter in ("gpu_native_surface_frames", "gpu_native_encode_frames"):
                    entry[counter] = gpu.get(counter)
                native_surface = gpu.get("gpu_native_surface_frames")
                native_encode = gpu.get("gpu_native_encode_frames")
                if not isinstance(native_surface, int) or isinstance(native_surface, bool) or native_surface <= 0:
                    entry["error"] = "native GPU surface contract missing or empty"
                elif native_surface != expected_frames:
                    entry["error"] = f"native GPU surfaces mismatch: expected {expected_frames}, got {native_surface}"
                elif not isinstance(native_encode, int) or isinstance(native_encode, bool) or native_encode != expected_frames:
                    entry["error"] = f"native NVENC frames mismatch: expected {expected_frames}, got {native_encode}"
                elif get(data, "job", "gpu", "effective_backend") != "vulkan":
                    entry["error"] = f"effective backend must be vulkan, got {get(data, 'job', 'gpu', 'effective_backend')!r}"
                elif get(data, "job", "surface_handoff_path") != "direct":
                    entry["error"] = f"surface handoff must be direct, got {get(data, 'job', 'surface_handoff_path')!r}"
                else:
                    gpu = get(data, "job", "gpu") or {}
                    software_encode = gpu.get("software_encode_frames")
                    pipe_fallback = gpu.get("video_pipe_fallback_frames")
                    entry["software_encode_frames"] = software_encode
                    entry["video_pipe_fallback_frames"] = pipe_fallback
                    if not isinstance(software_encode, int) or isinstance(software_encode, bool):
                        entry["error"] = "software encode fallback counter missing or invalid"
                    elif software_encode != 0:
                        entry["error"] = f"software encode fallback frames must be 0, got {software_encode}"
                    elif not isinstance(pipe_fallback, int) or isinstance(pipe_fallback, bool):
                        entry["error"] = "video pipe fallback counter missing or invalid"
                    elif pipe_fallback != 0:
                        entry["error"] = f"video pipe fallback frames must be 0, got {pipe_fallback}"
                    elif not isinstance(gpu.get("gpu_surface_copy_frames"), int) or isinstance(gpu.get("gpu_surface_copy_frames"), bool):
                        entry["error"] = "GPU surface-copy counter missing or invalid"
                    elif gpu["gpu_surface_copy_frames"] != 0:
                        entry["error"] = f"GPU surface copies must be 0, got {gpu['gpu_surface_copy_frames']}"
                    elif not isinstance(gpu.get("cpu_pixel_readback_bytes"), int) or isinstance(gpu.get("cpu_pixel_readback_bytes"), bool):
                        entry["error"] = "CPU pixel readback byte counter missing or invalid"
                    elif gpu["cpu_pixel_readback_bytes"] != 0:
                        entry["error"] = f"CPU pixel readback bytes must be 0, got {gpu['cpu_pixel_readback_bytes']}"
                    elif not isinstance(gpu.get("encoder_staging_copy_bytes"), int) or isinstance(gpu.get("encoder_staging_copy_bytes"), bool):
                        entry["error"] = "encoder staging-copy byte counter missing or invalid"
                    elif gpu["encoder_staging_copy_bytes"] != 0:
                        entry["error"] = f"encoder staging-copy bytes must be 0, got {gpu['encoder_staging_copy_bytes']}"
                    elif not math.isfinite(process_wall_ms) or process_wall_ms <= 0:
                        entry["error"] = "process wall time must be positive and finite"
                    else:
                        ffprobe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-count_frames", "-show_entries", "stream=nb_read_frames,r_frame_rate", "-of", "json", str(output)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                        if ffprobe.returncode != 0:
                            entry["error"] = f"ffprobe failed: {ffprobe.stderr[-1000:]}"
                        else:
                            probe = json.loads(ffprobe.stdout)
                            streams = probe.get("streams", [])
                            encoded_frames = int(streams[0].get("nb_read_frames", -1)) if len(streams) == 1 else -1
                            if encoded_frames != expected_frames:
                                entry["error"] = f"encoded frame count mismatch: expected {expected_frames}, got {encoded_frames if encoded_frames >= 0 else None}"
                            entry["encoded_frames"] = encoded_frames if encoded_frames >= 0 else None
                            if len(streams) == 1:
                                try:
                                    encoded_fps = Fraction(streams[0].get("r_frame_rate", "0/1"))
                                except (ValueError, ZeroDivisionError):
                                    encoded_fps = Fraction(0, 1)
                                entry["encoded_fps"] = f"{encoded_fps.numerator}/{encoded_fps.denominator}"
                                if encoded_fps != Fraction(case_fps, 1):
                                    entry["error"] = f"encoded frame rate mismatch: expected {case_fps}/1, got {entry['encoded_fps']}"
                            else:
                                entry["encoded_fps"] = None
        except (OSError, json.JSONDecodeError, TypeError, ValueError, AttributeError, subprocess.SubprocessError) as exc:
            entry["error"] = f"invalid timing sidecar: {exc}"
    result["cases"].append(entry)

failed = [c for c in result["cases"] if c["exit_code"] != 0 or c["error"]]
result["certification_eligible"] = (manifest is not None and len(result["cases"]) == 17
                                    and not source_dirty and commit != "unknown" and bool(binary_sha256)
                                    and bool(manifest_sha256) and bool(content_pack_id) and bool(gpu)
                                    and bool(native_library_versions) and not failed)
if not result["certification_eligible"]:
    result["certification_blockers"] = []
    if manifest is None:
        result["certification_blockers"].append("complete versioned corpus manifest was not run")
    if len(result["cases"]) != 17:
        result["certification_blockers"].append("report does not contain 17 corpus cases")
    if source_dirty:
        result["certification_blockers"].append("source worktree is dirty")
    if commit == "unknown":
        result["certification_blockers"].append("source commit unavailable")
    if not binary_sha256:
        result["certification_blockers"].append("binary SHA-256 unavailable")
    if not manifest_sha256:
        result["certification_blockers"].append("manifest SHA-256 unavailable")
    if not content_pack_id:
        result["certification_blockers"].append("content pack identity unavailable")
    if not gpu:
        result["certification_blockers"].append("GPU hardware identity unavailable")
    if not native_library_versions:
        result["certification_blockers"].append("FFmpeg build identity unavailable")
result["case_gate_pass"] = not failed
report_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
print(json.dumps({"report": str(report_path), "cases": len(result["cases"]), "failed": len(failed),
                  "case_gate_pass": result["case_gate_pass"],
                  "certification_eligible": result["certification_eligible"]}))
if failed: raise SystemExit(1)
PY

echo "GPU_BASELINE_REPORT=$REPORT_JSON"
