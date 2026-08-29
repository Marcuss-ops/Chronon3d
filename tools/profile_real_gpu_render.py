#!/usr/bin/env python3
"""Build the canonical real-video GPU render profile from a timing sidecar.

Missing telemetry is emitted as null (never as a guessed zero).  The sidecar
is the authority for render/copy/cache counters; an optional hardware CSV is
used for time-series percentiles.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from pathlib import Path


def pct(values, p):
    values = sorted(float(x) for x in values if x is not None)
    if not values:
        return None
    if len(values) == 1:
        return values[0]
    k = (len(values) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    if lo == hi:
        return values[lo]
    return values[lo] + (values[hi] - values[lo]) * (k - lo)


def stats(values):
    values = [x for x in values if x is not None]
    if not values:
        return {"total_ms": None, "avg_ms_per_frame": None,
                "p50_ms": None, "p95_ms": None, "max_ms": None}
    return {"total_ms": sum(values), "avg_ms_per_frame": statistics.mean(values),
            "p50_ms": pct(values, 50), "p95_ms": pct(values, 95),
            "max_ms": max(values)}


def frame_values(frames, path):
    out = []
    for frame in frames:
        value = frame
        for key in path.split("."):
            if not isinstance(value, dict):
                value = None
                break
            value = value.get(key)
        if isinstance(value, (int, float)):
            out.append(float(value))
    return out


def load_hardware(path):
    if not path:
        return None
    rows = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            values = {}
            for key, value in row.items():
                if key == "timestamp":
                    continue
                try:
                    values[key] = float(value)
                except (TypeError, ValueError):
                    pass
            rows.append(values)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sidecar", type=Path)
    ap.add_argument("--hardware-csv", type=Path)
    ap.add_argument("--json", type=Path, required=True)
    ap.add_argument("--markdown", type=Path)
    args = ap.parse_args()
    data = json.loads(args.sidecar.read_text())
    frames = data.get("frame_times_ms", [])
    job = data.get("job", {})
    gpu = job.get("gpu", {})
    cpu = job.get("cpu_breakdown", {})
    image = job.get("image", {})
    enc = job.get("encoder", {})
    summary = data.get("summary", {})
    wall = job.get("job_wall_ms") or data.get("wall_time_ms")
    render = job.get("render_loop_wall_ms") or data.get("render_ms")

    phases = {}
    for name in ("engine_init_ms", "backend_init_ms", "plan_read_ms", "plan_parse_ms",
                 "plan_validate_ms", "plan_compile_ms", "graph_compile_ms", "prepare_ms",
                 "render_loop_wall_ms", "encoder_finalize_ms", "mux_finalize_ms",
                 "output_finalize_ms", "validation_ms", "ffprobe_ms", "sha256_ms"):
        phases[name] = job.get(name)
    if wall is not None:
        phases["wall_ms"] = wall
    phase_pct = {k: (v / wall * 100.0 if isinstance(v, (int, float)) and wall else None)
                 for k, v in phases.items()}

    render_fields = {
        "timeline_eval_ms": "render.timeline_eval_ms",
        "graph_prepare_ms": "render.graph_prepare_ms",
        "graph_execute_ms": "render.graph_execute_ms",
        "compositing_ms": "render.compositing_ms",
        "effects_ms": "render.effects_ms",
        "surface_management_ms": "render.surface_management_ms",
        "backend_overhead_ms": "render.backend_overhead_ms",
        "accounted_cpu_ms": "render.accounted_cpu_ms",
        "unaccounted_cpu_ms": "render.unaccounted_cpu_ms",
    }
    render_cpu = {name: stats(frame_values(frames, path)) for name, path in render_fields.items()}
    counters = {}
    for name in ("compiled_graph_refresh", "cache_eval", "dirty_eval", "input_resolve",
                 "framebuffer_lifetime", "node_schedule", "node_dispatch",
                 "node_execute_actual", "node_overhead", "predicted_bbox", "state_assign",
                 "telemetry_emit"):
        key = name + "_ms"
        counters[name] = stats([float(cpu[key])] if isinstance(cpu.get(key), (int, float)) else [])

    def counter_group(names):
        return {name: gpu.get(name) for name in names}

    decode = counter_group(("decoder_backend", "video_decode_frames", "video_decode_native_surface_frames",
        "video_decode_software_frames", "video_decode_native_fallback_frames", "video_decode_wall_ms",
        "video_decode_hw_transfer_wall_ms", "video_decode_sws_wall_ms", "video_decode_framebuffer_wall_ms",
        "video_prefetch_hits", "video_prefetch_misses", "video_prefetch_wait_us",
        "video_prefetch_queue_depth_peak"))
    zero_copy = counter_group(("gpu_readback_bytes", "gpu_upload_bytes", "gpu_upload_full_surface_bytes",
        "gpu_upload_region_bytes", "cpu_pixel_readback_count", "cpu_pixel_readback_bytes",
        "encoder_staging_copy_bytes", "gpu_native_surface_frames", "gpu_native_encode_frames",
        "nv12_to_rgba_frames", "rgba_to_nv12_frames", "gpu_surface_copy_frames",
        "video_pipe_fallback_frames", "video_native_fallback_frames", "gpu_surface_create_failures",
        "gpu_encode_failures", "hwframe_transfer_to_cpu_frames", "software_encode_frames"))
    gpu_group = counter_group(("gpu_execute_ms", "gpu_submit_cpu_ms", "gpu_wait_cpu_ms", "gpu_submissions",
        "passes_executed", "gpu_nodes", "cuda_composite_frames", "cuda_composite_wall_us",
        "interop_ring_wait_count", "interop_ring_wait_us", "cuda_vulkan_wait_count",
        "cuda_vulkan_wait_submit_us", "cuda_vulkan_signal_count", "cuda_vulkan_signal_submit_us",
        "frame_slot_wait_count", "frame_slot_wait_us", "frame_batch_drain_wait_count",
        "frame_batch_drain_wait_us", "standalone_wait_count", "standalone_wait_us",
        "cuda_encode_event_wait_count", "cuda_encode_event_wait_us", "cuda_encode_queue_peak"))
    watermark = {k: image.get(k) for k in ("resolve_ms", "decode_ms", "convert_ms", "upload_ms",
                                            "draw_ms", "decode_count", "draw_count")}
    upload_breakdown = gpu.get("upload_breakdown", {})
    watermark["upload_count"] = upload_breakdown.get("gpu_upload_image_full_count", 0) + upload_breakdown.get("gpu_upload_image_region_count", 0)
    watermark["upload_bytes"] = upload_breakdown.get("gpu_upload_image_bytes")
    watermark.update({k: data.get("cache", {}).get(k) for k in ("image_cache_hits", "image_cache_misses",
                                                                 "gpu_asset_cache_hits", "gpu_asset_cache_misses")})
    cache = data.get("cache", {})
    cache_rates = {}
    for key in ("node", "image", "font", "glyph", "gpu_asset"):
        hit, miss = cache.get(key + "_cache_hits"), cache.get(key + "_cache_misses")
        cache_rates[key] = ((hit / (hit + miss) * 100.0) if isinstance(hit, int) and isinstance(miss, int) and hit + miss else None)

    # Keep the acceptance contract explicit.  The native-export gates and the
    # stricter direct-YUV gates are intentionally separate: the current
    # Vulkan/CUDA baseline is native and readback-free, but still performs the
    # full-frame NV12<->RGBA conversions that the DirectCudaYuvProgram must
    # remove.  Never collapse these into one optimistic boolean.
    gate_values = {**zero_copy, **gpu_group}
    native_gate_names = (
        "gpu_readback_bytes", "encoder_staging_copy_bytes",
        "hwframe_transfer_to_cpu_frames", "software_encode_frames",
        "video_native_fallback_frames", "video_pipe_fallback_frames",
        "gpu_surface_create_failures", "gpu_encode_failures",
    )
    # A one-time static watermark upload is allowed and is reported
    # separately.  The direct-YUV frame contract forbids per-frame video
    # conversion/copy/readback, not the initial residency upload of an asset.
    direct_yuv_gate_names = native_gate_names + (
        "nv12_to_rgba_frames", "rgba_to_nv12_frames",
        "gpu_surface_copy_frames",
    )
    gate_report = {
        "native_export": {
            name: {"value": gate_values.get(name),
                   "pass": gate_values.get(name) == 0}
            for name in native_gate_names
        },
        "direct_yuv_zero_copy": {
            name: {"value": gate_values.get(name),
                   "pass": gate_values.get(name) == 0}
            for name in direct_yuv_gate_names
        },
    }
    gate_report["native_export"]["all_pass"] = all(
        item["pass"] for key, item in gate_report["native_export"].items()
        if key != "all_pass")
    gate_report["direct_yuv_zero_copy"]["all_pass"] = all(
        item["pass"] for key, item in gate_report["direct_yuv_zero_copy"].items()
        if key != "all_pass")

    hardware = None
    rows = load_hardware(args.hardware_csv)
    if rows:
        hardware = {"samples": len(rows), "metrics": {}}
        for key in rows[0]:
            values = [r[key] for r in rows if key in r]
            hardware["metrics"][key] = {"avg": statistics.mean(values), "p50": pct(values, 50),
                                         "p95": pct(values, 95), "peak": max(values)}

    ranking = []
    rank_candidates = [("render_loop_wall_ms", render, "CPU_WAIT", False),
                       ("render.graph_execute_ms", render_cpu["graph_execute_ms"].get("total_ms"), "CPU_COMPUTE", True),
                       ("render.backend_overhead_ms", render_cpu["backend_overhead_ms"].get("total_ms"), "DRIVER", True),
                       ("render.node_dispatch_ms", counters["node_dispatch"].get("total_ms"), "CPU_COMPUTE", True),
                       ("render.node_execute_actual_ms", counters["node_execute_actual"].get("total_ms"), "GPU_COMPUTE", True),
                       ("render.timeline_eval_ms", render_cpu["timeline_eval_ms"].get("total_ms"), "CPU_COMPUTE", True),
                       ("render.graph_prepare_ms", render_cpu["graph_prepare_ms"].get("total_ms"), "CPU_COMPUTE", True),
                       ("render.surface_management_ms", render_cpu["surface_management_ms"].get("total_ms"), "MEMORY", True),
                       ("gpu_wait_cpu_ms", gpu_group.get("gpu_wait_cpu_ms"), "GPU_WAIT", False),
                       ("encoder_submit_cpu_ms", enc.get("submit_cpu_ms"), "ENCODE", False),
                       ("image.convert_ms", watermark.get("convert_ms"), "CPU_COMPUTE", False),
                       ("cuda_composite_wall_us", (gpu_group.get("cuda_composite_wall_us") / 1000.0 if gpu_group.get("cuda_composite_wall_us") is not None else None), "GPU_COMPUTE", False),
                       ("prepare_ms", phases.get("prepare_ms"), "STARTUP", False),
                       ("encoder_finalize_ms", phases.get("encoder_finalize_ms"), "ENCODE", False)]
    for name, total, kind, inclusive in sorted(rank_candidates, key=lambda x: x[1] or -1, reverse=True):
        if total is not None:
            ranking.append({"rank": len(ranking) + 1, "phase": name, "total_ms": total,
                            "percent_wall": total / wall * 100.0 if wall else None,
                            "ms_per_frame": total / len(frames) if frames else None, "type": kind,
                            "inclusive_measurement": inclusive})

    report = {"schema": "chronon3d.real_gpu_render_profile.v1", "source_sidecar": str(args.sidecar),
              "frames": len(frames), "wall_ms": wall, "end_to_end": phases, "phase_percent_wall": phase_pct,
              "nvdec_video_input": decode, "render_graph_cpu": render_cpu, "render_counters": counters,
              "watermark_image": watermark, "gpu_vulkan_cuda": gpu_group, "zero_copy": zero_copy,
              "nvenc": {k: enc.get(k) for k in ("submit_cpu_ms", "backpressure_wait_ms", "flush_ms",
                                                   "packet_receive_ms", "mux_packet_ms", "device_ms")},
              "framebuffer": data.get("memory", {}), "cache": {**cache, "hit_rate_percent": cache_rates},
              "gates": gate_report,
              "hardware": hardware, "first_frame_vs_steady_state": {
                  "first_frame_ms": summary.get("first_frame_ms"), "steady_state_avg_ms": summary.get("steady_avg_ms"),
                  "steady_state_p50_ms": summary.get("steady_p50_ms"), "steady_state_p95_ms": summary.get("steady_p95_ms"),
                  "steady_state_max_ms": max(frame_values(frames[10:], "wall_duration_ms")) if len(frames) > 10 else None,
                  "steady_state_fps": 1000.0 / summary["steady_avg_ms"] if summary.get("steady_avg_ms") else None},
              "bottleneck_ranking": ranking,
              "known_accounted_ms": render,
              "unaccounted_ms": None,
              "accounting_note": "known_accounted_ms uses the top-level render loop; ranking entries marked inclusive_measurement are children and must not be summed."}
    report["unaccounted_ms"] = wall - render if wall is not None and render is not None else None
    report["unaccounted_percent"] = report["unaccounted_ms"] / wall * 100.0 if wall else None
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(report, indent=2) + "\n")
    if args.markdown:
        lines = ["# Real GPU render profile", "", f"- Frames: {len(frames)}", f"- Wall: {wall:.3f} ms" if wall else "- Wall: unavailable", "", "## Total time", "", "```text"]
        for label, value in (("render", render), ("outside render", report["unaccounted_ms"])):
            if value is not None and wall:
                lines.append(f"{label:14} {value:10.3f} ms  {'#' * max(1, round(value / wall * 40))}")
        lines += ["```", "", "## TOP bottleneck", "", "| Rank | Phase | Total ms | % wall | ms/frame | Type | Inclusive |", "|---:|---|---:|---:|---:|---|:---:|"]
        lines += [f"| {x['rank']} | {x['phase']} | {x['total_ms']:.3f} | {x['percent_wall']:.2f} | {x['ms_per_frame']:.3f} | {x['type']} | {'yes' if x['inclusive_measurement'] else 'no'} |" for x in ranking]
        lines += ["", "## Gate status", "", "| Contract | Status |", "|---|:---:|"]
        lines += [f"| native export | {'PASS' if gate_report['native_export']['all_pass'] else 'FAIL'} |",
                  f"| direct-YUV frame path | {'PASS' if gate_report['direct_yuv_zero_copy']['all_pass'] else 'FAIL'} |",
                  "", "The direct-YUV contract allows one-time static asset residency upload; it forbids per-frame YUV/RGBA conversion and surface copies."]
        lines += ["", "## Interpretation", "", "Values absent from the sidecar remain null; no missing counter is inferred as zero.", "Inclusive child counters (graph/backend/dispatch) are ranked for diagnosis but are excluded from the accounting sum."]
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
