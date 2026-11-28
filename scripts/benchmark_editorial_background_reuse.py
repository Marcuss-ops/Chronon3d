#!/usr/bin/env python3
"""Compare repeated isolated background renders with one persistent timeline.

This is a GPU-only, self-contained editorial fixture. It deliberately uses the
same content-addressed photograph and phrase card in both variants, and records
Chronon's own decode, GPU upload/cache, residency, and wall-time telemetry.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import platform
import statistics
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_PLAN = ROOT / "docs/evidence/gel-01-core-bloom-20261105/test_with_image_bg.plan.json"
COUNTERS = (
    "image_decode_count",
    "gpu_upload_bytes",
    "gpu_upload_image_bytes",
    "gpu_asset_cache_hits",
    "gpu_asset_cache_misses",
    "gpu_resident_bytes",
    "gpu_submissions",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(command: list[str], *, cwd: Path, env: dict[str, str]) -> tuple[float, str]:
    started = time.perf_counter()
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    elapsed = (time.perf_counter() - started) * 1000
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}\n{result.stdout[-8000:]}")
    return elapsed, result.stdout


def make_plan(template: dict, frames: int, persistent: bool) -> dict:
    plan = copy.deepcopy(template)
    plan["job_id"] = "editorial-background-persistent" if persistent else "editorial-background-independent"
    duration = frames if persistent else 1
    plan["canvas"]["duration_frames"] = duration
    plan["output"]["path"] = "unused.raw"
    backdrop, phrase = plan["layers"]
    backdrop["id"] = "persistent_editorial_background"
    backdrop["start_frame"] = 0
    backdrop["duration_frames"] = duration
    if persistent:
        phrase_layers = []
        for frame in range(frames):
            current = copy.deepcopy(phrase)
            current["id"] = f"phrase_{frame:02d}"
            current["start_frame"] = frame
            current["duration_frames"] = 1
            current["text"] = f"Editorial transition {frame + 1}"
            phrase_layers.append(current)
        plan["layers"] = [backdrop, *phrase_layers]
    else:
        phrase["id"] = "phrase"
        phrase["start_frame"] = 0
        phrase["duration_frames"] = 1
        plan["layers"] = [backdrop, phrase]
    return plan


def parse_report(path: Path) -> dict:
    report = json.loads(path.read_text(encoding="utf-8"))
    job = report.get("job", {})
    gpu = job.get("gpu", {})
    image = job.get("image", {})
    cache = report.get("cache", {})
    values = {
        "timing_sidecar": str(path),
        "effective_backend": gpu.get("effective_backend"),
        "execution_downgrade_count": job.get("execution_downgrade_count"),
        "gpu_submissions": gpu.get("gpu_submissions"),
        "image_decode_count": image.get("decode_count"),
        "image_decode_ms": image.get("decode_ms"),
        "gpu_upload_bytes": gpu.get("gpu_upload_bytes_delta"),
        "gpu_upload_image_bytes": gpu.get("upload_breakdown", {}).get("gpu_upload_image_bytes"),
        "gpu_asset_cache_hits": cache.get("gpu_asset_cache_hits"),
        "gpu_asset_cache_misses": cache.get("gpu_asset_cache_misses"),
        "gpu_asset_cache_hits_delta": gpu.get("gpu_asset_cache_hits_delta"),
        "gpu_asset_cache_misses_delta": gpu.get("gpu_asset_cache_misses_delta"),
        "gpu_resident_bytes": gpu.get("vma_usage_bytes"),
        "vram_used_peak_mb": gpu.get("vram_used_peak_mb"),
        "render_time_ms": report.get("render_ms"),
        "wall_time_ms": report.get("wall_time_ms"),
        "process_wall_ms": job.get("process_wall_ms"),
        "frames_rendered": report.get("frames_total", 0),
    }
    if values["effective_backend"] != "vulkan" or not values["gpu_submissions"]:
        raise RuntimeError(f"{path}: Vulkan GPU execution was not proved; refusing a software/fallback result")
    if values["execution_downgrade_count"] != 0:
        raise RuntimeError(f"{path}: execution downgrade detected; refusing the benchmark sample")
    if not values["gpu_upload_image_bytes"] or not values["image_decode_count"]:
        raise RuntimeError(f"{path}: image decode/upload counters are missing")
    return values


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True, help="Vulkan-enabled chronon3d_cli binary")
    parser.add_argument("--output-dir", type=Path, required=True, help="Evidence directory (must be new or empty)")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--frames", type=int, default=8)
    args = parser.parse_args()
    if args.repetitions < 3 or args.frames < 2:
        parser.error("use at least 3 repetitions and 2 frames")
    cli = args.cli.resolve(strict=True)
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    if any(output_dir.iterdir()):
        parser.error(f"output directory must be empty: {output_dir}")
    template = json.loads(SOURCE_PLAN.read_text(encoding="utf-8"))
    env = os.environ.copy()
    nvidia_icd = Path("/usr/share/vulkan/icd.d/nvidia_icd.json")
    if nvidia_icd.exists():
        env["VK_DRIVER_FILES"] = str(nvidia_icd)
    rows: dict[str, list[dict]] = {"independent": [], "persistent": []}
    fixtures = {
        "independent": make_plan(template, args.frames, False),
        "persistent": make_plan(template, args.frames, True),
    }
    fixture_hashes = {}
    for name, plan in fixtures.items():
        fixture = output_dir / f"{name}.plan.json"
        fixture.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        fixture_hashes[name] = sha256(fixture)

    execution_order = ["independent", "persistent"]
    for variant in execution_order:
        run_count = args.repetitions * (args.frames if variant == "independent" else 1)
        for run_index in range(run_count):
            work = output_dir / "runs" / variant / f"run-{run_index:03d}"
            work.mkdir(parents=True)
            fixture = output_dir / f"{variant}.plan.json"
            result_path = work / "result.raw"
            command = [str(cli), "render-plan", "--input", str(fixture),
                       "--assets-root", str(ROOT), "--backend", "vulkan",
                       "--gpu-hot-path-mode", "require_gpu_native",
                       "--video-sink", "raw", "--output", str(result_path)]
            elapsed, output = run(command, cwd=work, env=env)
            sidecar = Path(str(result_path) + ".timing.json")
            if not sidecar.is_file():
                raise RuntimeError(f"Chronon timing sidecar missing in {work}\n{output[-3000:]}")
            row = parse_report(sidecar)
            row["process_wall_ms"] = elapsed
            row["output_bytes"] = result_path.stat().st_size if result_path.exists() else 0
            row["command"] = command
            rows[variant].append(row)
            if result_path.exists():
                result_path.unlink()

    def summarize(samples: list[dict]) -> dict:
        summary: dict[str, object] = {
            "samples": len(samples),
            "frames_rendered": sum(int(row.get("frames_rendered", 0)) for row in samples),
        }
        for key in ("process_wall_ms", "wall_time_ms", "render_time_ms", "image_decode_ms"):
            values = [float(row[key]) for row in samples if key in row]
            if values:
                summary[key + "_median"] = statistics.median(values)
                summary[key + "_samples"] = values
        for key in COUNTERS:
            values = [int(row[key]) for row in samples if key in row]
            if values:
                summary[key + "_total"] = sum(values)
                summary[key + "_per_render_median"] = statistics.median(values)
                frame_total = summary["frames_rendered"]
                if frame_total:
                    summary[key + "_per_frame"] = sum(values) / frame_total
        summary["total_process_wall_ms"] = sum(float(row.get("process_wall_ms", 0)) for row in samples)
        summary["total_render_time_ms"] = sum(float(row.get("render_time_ms", 0)) for row in samples)
        return summary

    def query(*command: str) -> str:
        try:
            return subprocess.run(command, check=True, text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return "unavailable"

    vulkan_summary = query("vulkaninfo", "--summary")
    vulkan_devices = [line.strip() for line in vulkan_summary.splitlines() if "deviceName" in line]
    result = {
        "schema": "editorial_background_reuse_benchmark.v1",
        "fixture": "same verified backdrop asset; independent one-frame renders versus one persistent multi-frame composition",
        "frames_per_repetition": args.frames,
        "repetitions": args.repetitions,
        "warmup": "none; process-local GPU cache is intentionally cold for each independent render",
        "execution_order": execution_order,
        "backend": "vulkan require_gpu_native; every run must report positive gpu_submissions and background uploads",
        "source_commit": query("git", "-C", str(ROOT), "rev-parse", "HEAD"),
        "source_tree_dirty": bool(query("git", "-C", str(ROOT), "status", "--porcelain")),
        "cli_sha256": sha256(cli),
        "source_plan_sha256": sha256(SOURCE_PLAN),
        "fixture_sha256": fixture_hashes,
        "host": {"platform": platform.platform(), "machine": platform.machine(),
                 "gpu": query("nvidia-smi", "--query-gpu=name,driver_version", "--format=csv,noheader"),
                 "vulkan_devices": vulkan_devices},
        "independent": summarize(rows["independent"]),
        "persistent": summarize(rows["persistent"]),
        "runs": rows,
    }
    independent = result["independent"]
    persistent = result["persistent"]
    comparison: dict[str, object] = {
        "frames_equal": independent["frames_rendered"] == persistent["frames_rendered"],
        "independent_frames": independent["frames_rendered"],
        "persistent_frames": persistent["frames_rendered"],
        "process_wall_ms_independent_total": independent["total_process_wall_ms"],
        "process_wall_ms_persistent_total": persistent["total_process_wall_ms"],
        "note": "Timing sidecars are per-render cold-process samples. Compare decode/upload per output frame; process wall also includes startup and host load.",
    }
    for metric in ("gpu_upload_image_bytes", "image_decode_count"):
        baseline = independent.get(metric + "_per_frame")
        composed = persistent.get(metric + "_per_frame")
        if baseline:
            comparison[metric + "_per_frame_reduction"] = 1 - float(composed) / float(baseline)
    result["comparison"] = comparison
    report = output_dir / "report.json"
    report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
