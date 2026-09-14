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
        --assets-root)         ASSETS_ROOT="$2"; shift 2 ;;
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
export CLI_BIN OUTPUT_DIR CORPUS_FILE FRAMES WARMUP_FRAMEBUFFERS BACKEND HARDWARE ENCODER_BACKEND GPU_HOT_PATH ENCODE_PRESET ASSETS_ROOT
export REPO_ROOT

python3 - "$REPORT_JSON" "${CASES[@]}" <<'PY'
import json
import os
import pathlib
import re
import subprocess
import sys
import time

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

manifest = None
if corpus_file:
    try:
        manifest = json.loads(pathlib.Path(corpus_file).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"invalid corpus manifest: {exc}")
    if not isinstance(manifest, dict):
        raise SystemExit("invalid corpus manifest: top-level value must be an object")
    manifest_cases = manifest.get("cases")
    if not isinstance(manifest_cases, list) or not manifest_cases:
        raise SystemExit("invalid corpus manifest: cases must be a non-empty list")
    cases = []
    seen_ids = set()
    for item in manifest_cases:
        if not isinstance(item, dict):
            raise SystemExit("invalid corpus manifest: each case must be an object")
        case_id = item.get("id")
        scene = item.get("scene")
        if not isinstance(case_id, str) or not case_id or not isinstance(scene, str) or not scene:
            raise SystemExit("invalid corpus manifest: each case needs non-empty id and scene")
        if case_id in seen_ids:
            raise SystemExit(f"invalid corpus manifest: duplicate case id {case_id!r}")
        seen_ids.add(case_id)
        cases.append(f"{case_id}={scene}")

def safe_name(value):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("._") or "case"

def get(data, *path):
    for key in path:
        if not isinstance(data, dict): return None
        data = data.get(key)
    return data

def numeric(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)

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
except (OSError, subprocess.CalledProcessError):
    commit = "unknown"

result = {
    "schema_version": 1,
    "commit": commit,
    "corpus": manifest.get("id") if manifest else None,
    "requested_cases": cases,
    "parameters": {"frames": frames, "warmup_framebuffers": warmup_framebuffers, "backend": backend,
                   "hardware": hardware, "encoder_backend": encoder_backend,
                   "gpu_hot_path_mode": gpu_hot_path,
                   "encode_preset": encode_preset},
    "cases": [],
}

for spec in cases:
    if "=" not in spec: raise SystemExit(f"invalid --case {spec!r}; expected ID=SCENE")
    case_id, scene = spec.split("=", 1)
    if not case_id or not scene: raise SystemExit(f"invalid --case {spec!r}")
    case_dir = out_root / safe_name(case_id)
    case_dir.mkdir(parents=True, exist_ok=True)
    output = case_dir / "render.mp4"
    log = case_dir / "render.log"
    command = [cli, "render", scene, "--frames", f"0-{frames - 1}",
               "--assets-root", assets_root, "--warmup-renderer",
               "--warmup-framebuffers", str(max(1, warmup_framebuffers)),
               "--backend", backend, "--hardware", hardware,
               "--encoder-backend", encoder_backend,
               "--gpu-hot-path-mode", gpu_hot_path, "--report", "-o", str(output)]
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
             "process_startup_ms": None, "prepare_ms": None,
             "warmup_ms": None, "steady_state_ms": None,
             "encode_drain_ms": None, "finalize_ms": None,
             "full_file_reread_count": None,
             "total_wall_ms": process_wall_ms, "attribution": None,
             "execution_path": None, "frames": None, "error": None}
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
            entry["frames"] = len(data.get("frame_times_ms", [])) or data.get("frames_total")
            entry["execution_path"] = job.get("execution_path")
            entry["process_startup_ms"] = phase(data, "startup_ms")
            entry["prepare_ms"] = phase(data, "prepare_ms")
            entry["warmup_ms"] = summary.get("warmup_avg_ms")
            entry["steady_state_ms"] = summary.get("steady_avg_ms")
            entry["encode_drain_ms"] = phase(data, "encode_drain_ms")
            entry["finalize_ms"] = phase(data, "finalize_ms")
            entry["attribution"] = data.get("phase_attribution") or data.get("exclusive_wall_timeline", {})
            entry["full_file_reread_count"] = get(data, "job", "full_file_reread_count")
            entry["windows"] = summary.get("windows", {})
            entry["prewarm_prepare_ms"] = get(data, "prepare_breakdown", "total_prepare_ms")
            accounted = get(data, "exclusive_wall_timeline", "accounted_percent")
            if not numeric(accounted):
                entry["error"] = "wall-time attribution missing accounted_percent"
            elif float(accounted) < 95.0:
                entry["error"] = f"wall-time attribution below 95%: {accounted:.2f}%"
            elif not isinstance(entry["windows"], dict) or not all(
                isinstance(entry["windows"].get(name), dict)
                for name in ("prewarm", "steady_state")
            ):
                entry["error"] = "prewarm/steady_state windows missing"
            elif any(not numeric(entry[name]) for name in (
                "process_startup_ms", "prepare_ms", "warmup_ms",
                "steady_state_ms", "encode_drain_ms", "finalize_ms")):
                entry["error"] = "required timing bucket missing or non-numeric"
            elif not isinstance(entry["full_file_reread_count"], int) or isinstance(
                entry["full_file_reread_count"], bool
            ) or entry["full_file_reread_count"] < 0:
                entry["error"] = "job.full_file_reread_count missing or invalid"
            if entry["frames"] != frames:
                entry["error"] = f"frame count mismatch: expected {frames}, got {entry['frames']}"
            if encoder_backend == "native":
                gpu = get(data, "job", "gpu") or {}
                native_surface = gpu.get("gpu_native_surface_frames")
                native_encode = gpu.get("gpu_native_encode_frames")
                if not isinstance(native_surface, (int, float)) or native_surface <= 0:
                    entry["error"] = "native GPU surface contract missing or empty"
                elif not isinstance(native_encode, (int, float)) or native_encode <= 0:
                    entry["error"] = "native NVENC contract missing or empty"
                elif get(data, "job", "surface_handoff_path") != "direct":
                    entry["error"] = "surface handoff is not direct"
        except (OSError, json.JSONDecodeError) as exc:
            entry["error"] = f"invalid timing sidecar: {exc}"
    result["cases"].append(entry)

report_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
failed = [c for c in result["cases"] if c["exit_code"] != 0 or c["error"]]
print(json.dumps({"report": str(report_path), "cases": len(result["cases"]), "failed": len(failed)}))
if failed: raise SystemExit(1)
PY

echo "GPU_BASELINE_REPORT=$REPORT_JSON"
