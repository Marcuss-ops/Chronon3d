#!/usr/bin/env python3
"""Run the B2 456-frame fixture and sample NVENC utilization via nvidia-smi."""

import hashlib
import json
import statistics
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = Path(__file__).resolve().parents[3]
PLAN = HERE / "static-text-456f.plan.json"
OUTPUT = HERE / "static-text-456f-moving-nvenc.mp4"
LOG = HERE / "static-text-456f-moving-nvenc.log"
RESULT = HERE / "b2-moving-results.json"
RUNS = HERE / "b2-moving-runs.json"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(f"usage: {sys.argv[0]} /path/to/chronon3d_cli [tag]", file=sys.stderr)
        return 2
    cli = Path(sys.argv[1]).resolve()
    tag = sys.argv[2] if len(sys.argv) == 3 else "moving"
    if not cli.is_file():
        print(f"CLI not found: {cli}", file=sys.stderr)
        return 2
    output = HERE / f"static-text-456f-{tag}-nvenc.mp4"
    log_path = HERE / f"static-text-456f-{tag}-nvenc.log"
    result_path = HERE / f"b2-{tag}-results.json"
    runs_path = HERE / f"b2-{tag}-runs.json"
    command = [
        str(cli), "render", "--plan", str(PLAN), "--start-frame", "0",
        "--end-frame", "455", "--output", str(output), "--assets-root",
        str(ROOT), "--backend", "vulkan", "--gpu-hot-path-mode",
        "require_gpu_native", "--hardware", "nvenc", "--encoder-backend",
        "native", "--log-level", "info",
    ]
    samples = []
    with log_path.open("wb") as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=log,
                                   stderr=subprocess.STDOUT)
        while process.poll() is None:
            query = subprocess.run(
                ["nvidia-smi", "--query-gpu=utilization.encoder",
                 "--format=csv,noheader,nounits"],
                capture_output=True, text=True, check=False)
            if query.returncode == 0:
                for line in query.stdout.splitlines():
                    try:
                        samples.append(float(line.strip()))
                    except ValueError:
                        pass
            time.sleep(0.2)
        exit_code = process.wait()
    sidecar_path = Path(str(output) + ".timing.json")
    sidecar = json.loads(sidecar_path.read_text()) if sidecar_path.is_file() else {}
    active = [value for value in samples if value > 0]
    result = {
        "command": command,
        "exit_code": exit_code,
        "frames_total": sidecar.get("frames_total"),
        "render_loop_fps": sidecar.get("summary", {}).get("render_loop_fps"),
        "end_to_end_fps": sidecar.get("summary", {}).get("end_to_end_fps"),
        "fast_path_reused_frames": sidecar.get("summary", {}).get("fast_path_reused_frames"),
        "frame_batch_drain_wait_us": sidecar.get("job", {}).get("gpu", {}).get("frame_batch_drain_wait_us"),
        "nvenc_frames": sidecar.get("job", {}).get("gpu", {}).get("nvenc_frames"),
        "sidecar_nvenc_utilization_avg": sidecar.get("job", {}).get("hardware", {}).get("nvenc_utilization_avg"),
        "nvidia_smi_samples_pct": samples,
        "nvidia_smi_avg_pct_including_idle": statistics.mean(samples) if samples else None,
        "nvidia_smi_avg_pct_nonzero_samples": statistics.mean(active) if active else None,
        "nvidia_smi_peak_pct": max(samples) if samples else None,
        "output_sha256": sha256(output) if output.is_file() else None,
        "output": str(output),
        "sidecar": str(sidecar_path) if sidecar_path.is_file() else None,
        "log": str(log_path),
    }
    result_path.write_text(json.dumps(result, indent=2) + "\n")
    runs = json.loads(runs_path.read_text()) if runs_path.is_file() else []
    result["run_index"] = len(runs) + 1
    runs.append(result)
    runs_path.write_text(json.dumps(runs, indent=2) + "\n")
    print(json.dumps(result, sort_keys=True))
    return 0 if exit_code == 0 and sidecar.get("frames_total") == 456 else 1


if __name__ == "__main__":
    raise SystemExit(main())
