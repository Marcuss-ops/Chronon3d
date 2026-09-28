#!/usr/bin/env python3
"""Aggregate and summarize Chronon3D timing sidecars (*.timing.json).

Computes median, p95, min, and max statistics across multi-job export sidecars
for phase attributions, startup/prepare breakdowns, per-frame GPU execution,
pipeline queue depth, deferred release counters, and NVENC utilization.

Supports Gate B2 verification (--gate-b2) and before/after comparisons (--compare).

Exit codes:
  0: PASS (or successful report when --gate-b2 is not specified)
  1: GATE_FAIL (one or more gate criteria failed)
  2: GATE_BLOCKED (no valid sidecars found or unreadable files)
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


def _percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    s = sorted(values)
    if len(s) == 1:
        return s[0]
    k = (len(s) - 1) * pct
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return s[int(k)]
    d0 = s[int(f)] * (c - k)
    d1 = s[int(c)] * (k - f)
    return d0 + d1


def _stats(values: list[float]) -> dict[str, float]:
    if not values:
        return {"median": 0.0, "p95": 0.0, "min": 0.0, "max": 0.0, "mean": 0.0}
    return {
        "median": _percentile(values, 0.50),
        "p95": _percentile(values, 0.95),
        "min": min(values),
        "max": max(values),
        "mean": sum(values) / len(values),
    }


def _find_json_files(paths: list[str]) -> list[Path]:
    files: list[Path] = []
    for p_str in paths:
        p = Path(p_str)
        if p.is_dir():
            files.extend(sorted(p.glob("**/*.timing.json")))
            if not files:
                files.extend(sorted(p.glob("*.json")))
        elif p.is_file():
            files.append(p)
    return files


def load_job_metrics(file_path: Path) -> Optional[dict[str, Any]]:
    try:
        with file_path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as exc:
        print(f"[WARN] Failed to read {file_path}: {exc}", file=sys.stderr)
        return None

    frames_total = data.get("frames_total")
    frame_times = data.get("frame_times_ms", [])
    if not frames_total or frames_total <= 0:
        frames_total = len(frame_times)
    frames_total = max(1, frames_total)

    phases = data.get("phase_attribution", {}).get("phases", {})
    startup = data.get("startup_breakdown", {})
    prepare = data.get("prepare_breakdown", {})
    job = data.get("job", {})
    gpu = job.get("gpu", {})
    pipe = job.get("pipeline", {})
    hw = job.get("hardware", {})
    cache = data.get("cache", {})

    # Per-frame extractions
    per_frame_graph_execute: list[float] = []
    per_frame_drain_wait: list[float] = []
    per_frame_device_span: list[float] = []

    for ft in frame_times:
        render = ft.get("render", {})
        gpu_obs = ft.get("gpu_observed", {})
        ge = render.get("graph_execute_ms")
        if ge is not None:
            per_frame_graph_execute.append(float(ge))
        dw = gpu_obs.get("drain_wait_ms")
        if dw is not None:
            per_frame_drain_wait.append(float(dw))
        ds = gpu_obs.get("device_span_ms")
        if ds is not None:
            per_frame_device_span.append(float(ds))

    drain_wait_us = gpu.get("frame_batch_drain_wait_us", 0) or 0
    drain_wait_us_per_frame = float(drain_wait_us) / float(frames_total)

    metrics = {
        "frames_total": frames_total,
        "wall_time_ms": float(data.get("wall_time_ms") or 0.0),
        # Phases
        "phase_process_startup_ms": float(phases.get("process_startup_ms") or 0.0),
        "phase_prepare_ms": float(phases.get("prepare_ms") or 0.0),
        "phase_render_loop_ms": float(phases.get("render_loop_ms") or 0.0),
        "phase_encode_drain_ms": float(phases.get("encode_drain_ms") or 0.0),
        "phase_ffprobe_ms": float(phases.get("ffprobe_ms") or 0.0),
        "phase_validation_ms": float(phases.get("validation_ms") or 0.0),
        # Startup
        "startup_vulkan_init_ms": float(startup.get("vulkan_instance_ms") or job.get("backend_init_ms") or 0.0),
        "startup_encoder_nvenc_ms": float(startup.get("encoder_open_nvenc_ms") or 0.0),
        "startup_plan_compile_ms": float(startup.get("plan_compile_ms") or job.get("plan_compile_ms") or 0.0),
        # Prepare
        "prepare_pool_warmup_ms": float(prepare.get("pool_warmup_ms") or 0.0),
        "prepare_font_preflight_ms": float(prepare.get("font_preflight_ms") or 0.0),
        "prepare_triple_arena_alloc_ms": float(prepare.get("triple_arena_alloc_ms") or 0.0),
        # Pipeline
        "pipeline_queue_depth_avg": float(pipe.get("queue_depth_average") or 0.0),
        "pipeline_render_slots_peak": float(pipe.get("render_slots_in_flight_peak") or 0.0),
        # Hardware
        "hw_nvenc_utilization_avg": float(hw.get("nvenc_utilization_avg") or 0.0),
        "hw_nvenc_utilization_peak": float(hw.get("nvenc_utilization_peak") or 0.0),
        "hw_gpu_utilization_avg": float(hw.get("gpu_utilization_avg") or 0.0),
        "hw_vram_used_peak_mb": float(hw.get("vram_used_peak_mb") or 0.0),
        # Drain & Deferred Releases
        "gpu_drain_wait_us_per_frame": drain_wait_us_per_frame,
        "gpu_drain_wait_total_us": float(drain_wait_us),
        "gpu_drain_wait_count": float(gpu.get("frame_batch_drain_wait_count") or 0.0),
        "gpu_deferred_flush_count": float(gpu.get("deferred_release_flush_count") or 0.0),
        "gpu_deferred_drain_wait_us": float(gpu.get("deferred_release_drain_wait_us") or 0.0),
        # Zero-copy gates
        "zero_copy_readback_bytes": float(gpu.get("cpu_pixel_readback_bytes") or 0.0),
        "zero_copy_surface_copies": float(gpu.get("gpu_surface_copy_frames") or 0.0),
        "zero_copy_staging_bytes": float(gpu.get("encoder_staging_copy_bytes") or 0.0),
        "zero_copy_cpu_upload_bytes": float(gpu.get("cpu_full_surface_upload_bytes") or 0.0),
        # Cache & Text
        "cache_font_hits": float(cache.get("font_cache_hits") or 0.0),
        "cache_font_misses": float(cache.get("font_cache_misses") or 0.0),
        "cache_glyph_hits": float(cache.get("glyph_cache_hits") or 0.0),
        "cache_glyph_misses": float(cache.get("glyph_cache_misses") or 0.0),
        # Per-frame averages per job
        "frame_graph_execute_median_ms": _percentile(per_frame_graph_execute, 0.50) if per_frame_graph_execute else 0.0,
        "frame_drain_wait_median_ms": _percentile(per_frame_drain_wait, 0.50) if per_frame_drain_wait else 0.0,
        "frame_device_span_median_ms": _percentile(per_frame_device_span, 0.50) if per_frame_device_span else 0.0,
    }
    return metrics


def aggregate_dataset(job_metrics_list: list[dict[str, Any]]) -> dict[str, dict[str, float]]:
    if not job_metrics_list:
        return {}
    keys = job_metrics_list[0].keys()
    agg: dict[str, dict[str, float]] = {}
    for k in keys:
        vals = [jm[k] for jm in job_metrics_list if k in jm]
        agg[k] = _stats(vals)
    return agg


def format_table(
    agg: dict[str, dict[str, float]],
    count: int,
    before_agg: Optional[dict[str, dict[str, float]]] = None,
    before_count: int = 0,
) -> str:
    lines: list[str] = []
    lines.append(f"Aggregate Performance Summary (Jobs: {count})")
    lines.append("=" * 88)

    categories = [
        ("Phase Attribution (Wall Time)", [
            ("Total Wall Time (ms)", "wall_time_ms"),
            ("Startup Phase (ms)", "phase_process_startup_ms"),
            ("Prepare Phase (ms)", "phase_prepare_ms"),
            ("Render Loop Phase (ms)", "phase_render_loop_ms"),
            ("Encode Drain Phase (ms)", "phase_encode_drain_ms"),
            ("FFprobe (ms)", "phase_ffprobe_ms"),
            ("Validation (ms)", "phase_validation_ms"),
        ]),
        ("Startup & Prepare Breakdown", [
            ("Vulkan Init (ms)", "startup_vulkan_init_ms"),
            ("NVENC Open (ms)", "startup_encoder_nvenc_ms"),
            ("Plan Compile (ms)", "startup_plan_compile_ms"),
            ("Pool Warmup (ms)", "prepare_pool_warmup_ms"),
            ("Font Preflight (ms)", "prepare_font_preflight_ms"),
            ("Triple Arena Alloc (ms)", "prepare_triple_arena_alloc_ms"),
        ]),
        ("Per-Frame GPU & Loop Breakdown", [
            ("Graph Execute per Frame (ms)", "frame_graph_execute_median_ms"),
            ("Drain Wait per Frame (ms)", "frame_drain_wait_median_ms"),
            ("Device Span per Frame (ms)", "frame_device_span_median_ms"),
        ]),
        ("Pipeline & Depth Telemetry", [
            ("Queue Depth Average", "pipeline_queue_depth_avg"),
            ("Render Slots Peak", "pipeline_render_slots_peak"),
        ]),
        ("Submission Ring Drain & Deferred Releases", [
            ("Drain Wait per Frame (us)", "gpu_drain_wait_us_per_frame"),
            ("Total Drain Wait (us)", "gpu_drain_wait_total_us"),
            ("Drain Wait Count", "gpu_drain_wait_count"),
            ("Deferred Flush Count", "gpu_deferred_flush_count"),
            ("Deferred Drain Wait (us)", "gpu_deferred_drain_wait_us"),
        ]),
        ("Hardware & NVENC Utilization", [
            ("NVENC Util Avg (%)", "hw_nvenc_utilization_avg"),
            ("NVENC Util Peak (%)", "hw_nvenc_utilization_peak"),
            ("GPU Util Avg (%)", "hw_gpu_utilization_avg"),
            ("VRAM Peak (MB)", "hw_vram_used_peak_mb"),
        ]),
        ("Zero-Copy Verification", [
            ("CPU Readback Bytes", "zero_copy_readback_bytes"),
            ("GPU Surface Copies", "zero_copy_surface_copies"),
            ("Encoder Staging Bytes", "zero_copy_staging_bytes"),
            ("CPU Upload Bytes", "zero_copy_cpu_upload_bytes"),
        ]),
        ("Cache & Text Counters", [
            ("Font Cache Hits", "cache_font_hits"),
            ("Font Cache Misses", "cache_font_misses"),
            ("Glyph Cache Hits", "cache_glyph_hits"),
            ("Glyph Cache Misses", "cache_glyph_misses"),
        ]),
    ]

    if before_agg:
        header = f"{'Metric':<34} | {'Before Median':>14} | {'After Median':>14} | {'Delta (%)':>10} | {'After p95':>11}"
        lines.append(header)
        lines.append("-" * len(header))
        for cat_title, fields in categories:
            lines.append(f"\n[{cat_title}]")
            for label, key in fields:
                curr_med = agg.get(key, {}).get("median", 0.0)
                curr_p95 = agg.get(key, {}).get("p95", 0.0)
                bef_med = before_agg.get(key, {}).get("median", 0.0)
                delta = curr_med - bef_med
                pct_str = f"{(delta / bef_med * 100.0):+6.1f}%" if bef_med != 0 else "    N/A"
                lines.append(f"{label:<34} | {bef_med:>14.2f} | {curr_med:>14.2f} | {pct_str:>10} | {curr_p95:>11.2f}")
    else:
        header = f"{'Metric':<36} | {'Median':>12} | {'p95':>12} | {'Min':>10} | {'Max':>10}"
        lines.append(header)
        lines.append("-" * len(header))
        for cat_title, fields in categories:
            lines.append(f"\n[{cat_title}]")
            for label, key in fields:
                s = agg.get(key, {})
                med = s.get("median", 0.0)
                p95 = s.get("p95", 0.0)
                mn = s.get("min", 0.0)
                mx = s.get("max", 0.0)
                lines.append(f"{label:<36} | {med:>12.2f} | {p95:>12.2f} | {mn:>10.2f} | {mx:>10.2f}")

    return "\n".join(lines)


def evaluate_gate_b2(agg: dict[str, dict[str, float]]) -> Tuple[bool, list[str], list[str]]:
    failures: list[str] = []
    warnings: list[str] = []

    drain_wait_per_frame = agg.get("gpu_drain_wait_us_per_frame", {}).get("median", 0.0)
    if drain_wait_per_frame > 50.0:
        failures.append(f"frame_batch_drain_wait_us_per_frame = {drain_wait_per_frame:.1f} us (> 50.0 us threshold)")

    nvenc_util = agg.get("hw_nvenc_utilization_avg", {}).get("median", 0.0)
    if nvenc_util < 90.0:
        failures.append(f"nvenc_utilization_avg = {nvenc_util:.1f}% (< 90.0% threshold)")

    queue_depth = agg.get("pipeline_queue_depth_avg", {}).get("median", 0.0)
    if queue_depth < 3.0:
        warnings.append(f"queue_depth_average = {queue_depth:.2f} (< 3.0 target)")

    passed = len(failures) == 0
    return passed, failures, warnings


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("input_paths", nargs="+", help="Directory or file path(s) to timing.json sidecars")
    parser.add_argument("--gate-b2", action="store_true", help="Enforce Wave B2 performance gate constraints")
    parser.add_argument("--compare", help="Directory with baseline sidecars to compare against")
    args = parser.parse_args(argv)

    files = _find_json_files(args.input_paths)
    if not files:
        print(f"[ERROR] No timing sidecar files found in: {args.input_paths}", file=sys.stderr)
        print("GATE_BLOCKED")
        return 2

    job_metrics: list[dict[str, Any]] = []
    for fp in files:
        m = load_job_metrics(fp)
        if m:
            job_metrics.append(m)

    if not job_metrics:
        print(f"[ERROR] Failed to extract metrics from {len(files)} file(s)", file=sys.stderr)
        print("GATE_BLOCKED")
        return 2

    agg = aggregate_dataset(job_metrics)

    before_agg: Optional[dict[str, dict[str, float]]] = None
    before_count = 0
    if args.compare:
        before_files = _find_json_files([args.compare])
        before_metrics: list[dict[str, Any]] = []
        for fp in before_files:
            m = load_job_metrics(fp)
            if m:
                before_metrics.append(m)
        if before_metrics:
            before_agg = aggregate_dataset(before_metrics)
            before_count = len(before_metrics)

    table = format_table(agg, len(job_metrics), before_agg, before_count)
    print(table)
    print()

    if args.gate_b2:
        passed, failures, warnings = evaluate_gate_b2(agg)
        for w in warnings:
            print(f"  [WARN] {w}")
        if not passed:
            for f in failures:
                print(f"  [FAIL] {f}")
            print(f"GATE_FAIL: {len(failures)} B2 gate requirement(s) failed")
            return 1
        print("GATE_PASS: Wave B2 pipeline depth constraints satisfied")
        return 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
