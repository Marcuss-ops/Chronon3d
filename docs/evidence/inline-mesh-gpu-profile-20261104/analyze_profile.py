#!/usr/bin/env python3
"""Validate and summarize the inline-mesh Vulkan GPU profile sidecars."""

from __future__ import annotations

import csv
import hashlib
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
COUNTS = (1, 10, 100, 1000, 10000)
WIDTH, HEIGHT, FRAMES = 128, 96, 31


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_case(triangles: int) -> dict:
    plan_path = HERE / "plans" / f"inline-mesh-{triangles}.plan.json"
    sidecar_path = HERE / f"profile-{triangles}.rgba.timing.json"
    log_path = HERE / f"profile-{triangles}.log"
    plan = json.loads(plan_path.read_text())
    sidecar = json.loads(sidecar_path.read_text())
    log = log_path.read_text()
    mesh = plan["layers"][0]["inline_mesh"]
    plan_triangles = len(mesh["indices"]) // 3
    gpu = sidecar["job"]["gpu"]
    summary = sidecar["summary"]
    verified = re.search(
        r"pre-encode artifact verified: (\d+) frames × (\d+) bytes = (\d+) bytes "
        r"\(format rgba\), sha256=([0-9a-f]{64})",
        log,
    )
    if not verified:
        raise ValueError(f"{triangles}: missing raw artifact verification in log")
    output_frames, frame_bytes, output_bytes = map(int, verified.groups()[:3])
    output_sha = verified.group(4)
    if plan_triangles != triangles:
        raise ValueError(f"{triangles}: plan contains {plan_triangles} triangles")
    if output_frames != FRAMES or sidecar.get("frames_total") != FRAMES:
        raise ValueError(f"{triangles}: expected {FRAMES} output frames")
    if frame_bytes != WIDTH * HEIGHT * 4 or output_bytes != frame_bytes * FRAMES:
        raise ValueError(f"{triangles}: unexpected RGBA byte count")
    if gpu.get("effective_backend") != "vulkan":
        raise ValueError(f"{triangles}: effective backend is not Vulkan")
    if gpu.get("pass_counts", {}).get("composite") != 99:
        raise ValueError(f"{triangles}: expected 99 composite passes")

    composite_ms = float(gpu["pass_breakdown_ms"]["composite"])
    total_gpu_ms = float(gpu["gpu_execute_ms"])
    nominal_candidate_checks = triangles * WIDTH * HEIGHT * FRAMES
    return {
        "triangles": triangles,
        "plan_sha256": sha256(plan_path),
        "plan_triangles": plan_triangles,
        "frames": FRAMES,
        "warmup_frames": summary["warmup_frames"],
        "measured_frames": summary["windows"]["steady_state"]["frame_count"],
        "resolution": [WIDTH, HEIGHT],
        "composite_gpu_ms_job": composite_ms,
        "composite_passes_job": gpu["pass_counts"]["composite"],
        "composite_gpu_ms_per_output_frame": composite_ms / FRAMES,
        "composite_gpu_ms_per_composite_pass": composite_ms / gpu["pass_counts"]["composite"],
        "total_gpu_ms_job": total_gpu_ms,
        "total_gpu_ms_per_output_frame": total_gpu_ms / FRAMES,
        "nominal_candidate_checks": nominal_candidate_checks,
        "nominal_full_canvas_checks_per_second": nominal_candidate_checks / (composite_ms / 1000.0),
        "steady_p50_cpu_frame_ms": summary["steady_p50_ms"],
        "steady_p95_cpu_frame_ms": summary["steady_p95_ms"],
        "render_loop_fps": summary["render_loop_fps"],
        "raw_output_bytes": output_bytes,
        "raw_output_sha256": output_sha,
        "frame_gpu_timestamp_attributed": bool(
            sidecar["frame_times_ms"][0]["gpu_observed"]["timestamp_attributed"]
        ),
        "gpu_submissions": gpu["gpu_submissions"],
        "timing_sidecar_sha256": sha256(sidecar_path),
    }


def write_csv(rows: list[dict]) -> None:
    path = HERE / "metrics.csv"
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    rows = [load_case(value) for value in COUNTS]
    reference = next(row for row in rows if row["triangles"] == 10)
    for row in rows:
        row["incremental_composite_gpu_ms_vs_10_triangles"] = (
            row["composite_gpu_ms_job"] - reference["composite_gpu_ms_job"]
            if row["triangles"] > 10 else None
        )
        delta_triangles = row["triangles"] - 10
        if delta_triangles > 0:
            delta_ms = row["composite_gpu_ms_job"] - reference["composite_gpu_ms_job"]
            row["incremental_triangle_pixel_checks_per_second_vs_10"] = (
                delta_triangles * WIDTH * HEIGHT * FRAMES / (delta_ms / 1000.0)
            )
        else:
            row["incremental_triangle_pixel_checks_per_second_vs_10"] = None
    (HERE / "results.json").write_text(json.dumps({
        "source_sha": "b4c50796c09bbdd0e2d7a86db2305f68d73c4784",
        "cli_sha256": "71209e5396667f8b51689c32fa8dbaf45bb2e6b78b1c0556ed2970076bf674b9",
        "inline_mesh_shader_sha256": "c4f564a4f52716f12aacc948f413a2fd3b5f0e92d10f1588ad10c16db1ffe412",
        "device": "NVIDIA RTX A4000",
        "driver": "595.99.02",
        "backend": "Vulkan compute; raw host-frame sink",
        "cuda_interop": False,
        "build": "Release; GCC 11.4.0; Vulkan ON; CUDA interop OFF; native FFmpeg ON; CLI target",
        "gpu_timing_source": "job.gpu.pass_breakdown_ms from Vulkan timestamp queries",
        "per_frame_gpu_attribution": False,
        "external_gpu_load_sampled_during_runs": False,
        "normalization_note": "candidate rate uses full canvas pixels; actual shader dispatch is clipped to each projected mesh bounding rectangle",
        "resolution": [WIDTH, HEIGHT],
        "fps": 30,
        "frames_per_case": FRAMES,
        "cases": rows,
    }, indent=2) + "\n")
    write_csv(rows)
    from PIL import Image, ImageDraw

    contact = Image.new("RGB", (320, 250 * len(COUNTS)), "white")
    draw = ImageDraw.Draw(contact)
    for index, count in enumerate(COUNTS):
        preview_count = count if count in (1, 10, 10000) else 10
        preview = Image.open(HERE / f"preview-{preview_count}.png").convert("RGBA")
        preview = preview.resize((256, 192), Image.Resampling.NEAREST)
        y = index * 250
        draw.text((12, y + 8), f"{count:,} triangles | 128x96 px", fill="black")
        contact.paste(preview, (32, y + 32), preview)
    contact.save(HERE / "preview-contact.png")

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    triangles = [row["triangles"] for row in rows]
    gpu_values = [row["composite_gpu_ms_per_output_frame"] for row in rows]
    throughput = [row["nominal_full_canvas_checks_per_second"] / 1e9 for row in rows]
    figure, axes = plt.subplots(2, 1, figsize=(9.6, 7.0), constrained_layout=True)
    axes[0].plot(triangles, gpu_values, marker="o", color="#1268b3")
    axes[0].set_xscale("log")
    axes[0].set_yscale("log")
    axes[0].set_ylim(0.01, 10)
    axes[0].set_ylabel("GPU composite timestamp (ms/frame)")
    axes[0].set_xticks(triangles, [f"{value:,}" for value in triangles])
    axes[0].set_xlabel("Triangles per inline mesh")
    axes[0].grid(True, which="both", alpha=0.3)
    for x, value in zip(triangles, gpu_values):
        axes[0].annotate(f"{value:.3f}", (x, value), xytext=(5, 7),
                         textcoords="offset points", fontsize=8)
    axes[1].plot(triangles, throughput, marker="o", color="#1268b3")
    axes[1].set_xscale("log")
    axes[1].set_ylabel("Nominal full-canvas triangle-pixel checks (G/s)")
    axes[1].set_xlabel("Triangles per inline mesh")
    axes[1].set_xticks(triangles, [f"{value:,}" for value in triangles])
    axes[1].grid(True, which="both", alpha=0.3)
    for x, value in zip(triangles, throughput):
        axes[1].annotate(f"{value:.2f}", (x, value), xytext=(5, 7),
                         textcoords="offset points", fontsize=8)
    figure.suptitle("Chronon3D inline_mesh.comp — RTX A4000\n31 frames/job; 5 warm-up + 26 measured; 128x96; job-level Vulkan timestamps")
    figure.savefig(HERE / "gpu-profile.png", dpi=150)
    figure.savefig(HERE / "gpu-profile.svg")
    plt.close(figure)
    print(f"wrote results for {len(rows)} cases to {HERE}")


if __name__ == "__main__":
    main()
