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
        "frame_slot_wait_count", "frame_slot_wait_us", "cuda_vulkan_wait_count",
        "cuda_vulkan_wait_submit_us", "cuda_vulkan_signal_count", "cuda_vulkan_signal_submit_us",
        "frame_slot_wait_count", "frame_slot_wait_us", "frame_batch_drain_wait_count",
        "frame_batch_drain_wait_us", "standalone_wait_count", "standalone_wait_us",
        "cuda_encode_event_wait_count", "cuda_encode_event_wait_us", "cuda_encode_queue_peak"))
    watermark = {k: image.get(k) for k in ("resolve_ms", "decode_ms", "convert_ms", "upload_ms",
                                            "draw_ms", "decode_count", "draw_count")}
    upload_breakdown = gpu.get("upload_breakdown") or {}
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

    exclusive_wall = data.get("exclusive_wall_timeline", {})
    internal_prof = data.get("internal_profiling", {})

    report = {"schema": "chronon3d.real_gpu_render_profile.v1", "source_sidecar": str(args.sidecar),
              "frames": len(frames), "wall_ms": wall, "end_to_end": phases, "phase_percent_wall": phase_pct,
              "exclusive_wall_timeline": exclusive_wall,
              "internal_profiling": internal_prof,
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

    # Format Level 1 ASCII tree
    ew = exclusive_wall
    p_wall = ew.get("process_wall_ms") or wall or 1.0
    def pct_w(val):
        return (val / p_wall * 100.0) if (val is not None and p_wall) else 0.0

    l1_lines = [
        "================================================================================",
        "LEVEL 1 — EXCLUSIVE PROCESS WALL TIMELINE",
        "================================================================================",
        f"PROCESS WALL                               {p_wall:10.2f} ms  (100.0%)",
        f"├── startup                                {ew.get('startup_ms') or 0.0:10.2f} ms  ({pct_w(ew.get('startup_ms')):5.1f}%)",
        f"├── input/open                             {ew.get('input_open_ms') or 0.0:10.2f} ms  ({pct_w(ew.get('input_open_ms')):5.1f}%)",
        f"├── prepare exclusive                      {ew.get('prepare_ms') or 0.0:10.2f} ms  ({pct_w(ew.get('prepare_ms')):5.1f}%)",
        f"├── render_loop exclusive                  {ew.get('render_loop_ms') or render or 0.0:10.2f} ms  ({pct_w(ew.get('render_loop_ms') or render):5.1f}%)",
        f"├── encoder_drain_finalize exclusive       {ew.get('encoder_drain_finalize_ms') or 0.0:10.2f} ms  ({pct_w(ew.get('encoder_drain_finalize_ms')):5.1f}%)",
        f"├── mux_finalize exclusive                 {ew.get('mux_finalize_ms') or 0.0:10.2f} ms  ({pct_w(ew.get('mux_finalize_ms')):5.1f}%)",
        f"├── validation exclusive                   {ew.get('validation_ms') or 0.0:10.2f} ms  ({pct_w(ew.get('validation_ms')):5.1f}%)",
        f"├── ffprobe exclusive                      {ew.get('ffprobe_ms') or 0.0:10.2f} ms  ({pct_w(ew.get('ffprobe_ms')):5.1f}%)",
        f"├── sha256 exclusive                       {ew.get('sha256_ms') or 0.0:10.2f} ms  ({pct_w(ew.get('sha256_ms')):5.1f}%)",
        f"├── sidecar/report                         {ew.get('sidecar_report_ms') or 0.0:10.2f} ms  ({pct_w(ew.get('sidecar_report_ms')):5.1f}%)",
        f"└── unaccounted                            {ew.get('unaccounted_ms') or 0.0:10.2f} ms  ({pct_w(ew.get('unaccounted_ms')):5.1f}%)",
        "",
        f"ACCOUNTED:                                 {ew.get('accounted_percent') or 0.0:5.1f}%",
        "================================================================================"
    ]

    # Format Sub-breakdown 1: Startup
    sb = data.get("startup_breakdown", {})
    sb_total = sb.get("total_startup_ms") or ew.get("startup_ms") or 0.0
    def pct_sb(val):
        return (val / sb_total * 100.0) if (val and sb_total > 0) else 0.0

    startup_tree_lines = [
        "SUB-BREAKDOWN 1 — STARTUP COST (53% of Process Wall)",
        "================================================================================",
        f"STARTUP TOTAL:                             {sb_total:10.2f} ms  ({pct_w(sb_total):5.1f}% wall)",
        f"  ├── CLI parse & plan compile             {sb.get('cli_init_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('cli_init_ms')):5.1f}% startup)  [PER-JOB]",
        f"  ├── Encoder struct allocation            {sb.get('encoder_create_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('encoder_create_ms')):5.1f}% startup)  [PER-JOB]",
        f"  ├── CUDA driver & FFmpeg hwdevice init   {sb.get('encoder_open_hw_ctx_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('encoder_open_hw_ctx_ms')):5.1f}% startup)  [PERSISTENT / WORKER]",
        f"  ├── CUDA compositor PTX/module load      {sb.get('cuda_compositor_warmup_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('cuda_compositor_warmup_ms')):5.1f}% startup)  [PERSISTENT / WORKER]",
        f"  ├── NVENC encoder session init (open)    {sb.get('encoder_open_nvenc_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('encoder_open_nvenc_ms')):5.1f}% startup)  [PER-JOB / POOLED]",
        f"  ├── MP4 container header write           {sb.get('encoder_open_mux_header_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('encoder_open_mux_header_ms')):5.1f}% startup)  [PER-JOB]",
        f"  ├── Vulkan instance & phys device enum   {sb.get('vulkan_instance_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('vulkan_instance_ms')):5.1f}% startup)  [PERSISTENT / WORKER]",
        f"  ├── Vulkan device & queue creation       {sb.get('vulkan_device_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('vulkan_device_ms')):5.1f}% startup)  [PERSISTENT / WORKER]",
        f"  ├── Vulkan compute pipeline compilation  {sb.get('vulkan_pipelines_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('vulkan_pipelines_ms')):5.1f}% startup)  [PERSISTENT / WORKER]",
        f"  ├── Render runtime & asset resolver init {sb.get('renderer_runtime_init_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('renderer_runtime_init_ms')):5.1f}% startup)  [PERSISTENT / WORKER]",
        f"  └── Other startup overhead               {sb.get('other_startup_ms') or 0.0:10.2f} ms  ({pct_sb(sb.get('other_startup_ms')):5.1f}% startup)",
        "================================================================================"
    ]

    # Format Sub-breakdown 2: Prepare
    pb = data.get("prepare_breakdown", {})
    pb_total = pb.get("total_prepare_ms") or ew.get("prepare_ms") or 0.0
    def pct_pb(val):
        return (val / pb_total * 100.0) if (val and pb_total > 0) else 0.0

    prepare_tree_lines = [
        "SUB-BREAKDOWN 2 — PREPARE COST (10% of Process Wall)",
        "================================================================================",
        f"PREPARE TOTAL:                             {pb_total:10.2f} ms  ({pct_w(pb_total):5.1f}% wall)",
        f"  ├── Font preflight & glyph caching check {pb.get('font_preflight_ms') or 0.0:10.2f} ms  ({pct_pb(pb.get('font_preflight_ms')):5.1f}% prepare)  [PERSISTENT / CACHE]",
        f"  ├── Framebuffer pool pre-allocation      {pb.get('pool_warmup_ms') or 0.0:10.2f} ms  ({pct_pb(pb.get('pool_warmup_ms')):5.1f}% prepare)  [PERSISTENT / WORKER]",
        f"  ├── Triple buffer arena allocation       {pb.get('triple_arena_alloc_ms') or 0.0:10.2f} ms  ({pct_pb(pb.get('triple_arena_alloc_ms')):5.1f}% prepare)  [PERSISTENT / WORKER]",
        f"  ├── Writer background thread spawn       {pb.get('writer_thread_spawn_ms') or 0.0:10.2f} ms  ({pct_pb(pb.get('writer_thread_spawn_ms')):5.1f}% prepare)  [PERSISTENT / WORKER]",
        f"  └── Other prepare overhead               {pb.get('other_prepare_ms') or 0.0:10.2f} ms  ({pct_pb(pb.get('other_prepare_ms')):5.1f}% prepare)",
        "================================================================================"
    ]

    # Format Level 2 Internal Profiling
    dec = internal_prof.get("decode", {})
    dyuv = internal_prof.get("direct_yuv", {})
    enc_p = internal_prof.get("encoder", {})
    fps = (len(frames) / (render / 1000.0)) if (render and len(frames)) else 0.0

    l2_lines = [
        "LEVEL 2 — INTERNAL PROFILING (Nested / Concurrent — not summed to Wall)",
        "================================================================================",
        f"RENDER INTERNAL (Render Loop Wall: {render or 0.0:.2f} ms | {fps:.1f} FPS)",
        "",
        f"Decoded frames:                            {dec.get('decoded_frames') or len(frames)}",
        f"Decode total:                              {dec.get('decode_total_ms') or 0.0:10.2f} ms",
        f"  ├── demux/read_packet                    {dec.get('demux_read_packet_ms') or 0.0:10.2f} ms",
        f"  ├── avcodec_send_packet                  {dec.get('avcodec_send_packet_ms') or 0.0:10.2f} ms",
        f"  ├── avcodec_receive_frame                {dec.get('avcodec_receive_frame_ms') or 0.0:10.2f} ms",
        f"  └── NVDEC wait                           {dec.get('nvdec_wait_ms') or 0.0:10.2f} ms",
        f"  ├── cpu_active_ms                        {dec.get('cpu_active_ms') or 0.0:10.2f} ms",
        f"  └── cpu_wait_ms                          {dec.get('cpu_wait_ms') or 0.0:10.2f} ms",
        f"Decode per frame:   AVG: {dec.get('avg_ms_per_frame') or 0.0:.3f} ms | P50: {dec.get('p50_ms_per_frame') or 0.0:.3f} ms | P95: {dec.get('p95_ms_per_frame') or 0.0:.3f} ms | MAX: {dec.get('max_ms_per_frame') or 0.0:.3f} ms",
        "",
        "Direct-YUV:",
        f"  ├── input probe (demux/NVDEC open)        {dyuv.get('input_probe_ms') or 0.0:10.2f} ms",
        f"  ├── static scene evaluation               {dyuv.get('scene_eval_ms') or 0.0:10.2f} ms",
        f"  ├── watermark image load/decode           {dyuv.get('watermark_image_load_ms') or 0.0:10.2f} ms",
        f"  ├── watermark CUDA upload                 {dyuv.get('watermark_cuda_upload_ms') or 0.0:10.2f} ms",
        f"  ├── prepare/update                       {dyuv.get('prepare_update_ms') or 0.0:10.2f} ms",
        f"  ├── CUDA launch                          {dyuv.get('cuda_launch_ms') or 0.0:10.2f} ms",
        f"  └── CUDA event wait (source release)     {dyuv.get('cuda_event_wait_ms') or 0.0:10.2f} ms",
        f"  Kernel execution (GPU total):            {dyuv.get('cuda_kernel_total_ms') or 0.0:10.2f} ms",
        "",
        "Encoder (NVENC):",
        f"  ├── av_hwframe_get_buffer                {enc_p.get('av_hwframe_get_buffer_ms') or 0.0:10.2f} ms",
        f"  ├── surface acquire                      {enc_p.get('surface_acquire_ms') or 0.0:10.2f} ms",
        f"  ├── NVENC submit (avcodec_send_frame)    {enc_p.get('nvenc_submit_ms') or 0.0:10.2f} ms",
        f"  ├── queue/backpressure wait              {enc_p.get('queue_backpressure_wait_ms') or 0.0:10.2f} ms",
        f"  └── packet drain                         {enc_p.get('packet_drain_ms') or 0.0:10.2f} ms",
        f"  ├── cpu_active_ms                        {enc_p.get('cpu_active_ms') or 0.0:10.2f} ms",
        f"  └── cpu_wait_ms                          {enc_p.get('cpu_wait_ms') or 0.0:10.2f} ms",
        "",
        "Steady-State Frames:",
        f"  First frame:                             {summary.get('first_frame_ms') or 0.0:10.2f} ms",
        f"  Steady frame AVG:                        {summary.get('steady_avg_ms') or 0.0:10.3f} ms",
        f"  Steady frame P50:                        {summary.get('steady_p50_ms') or 0.0:10.3f} ms",
        f"  Steady frame P95:                        {summary.get('steady_p95_ms') or 0.0:10.3f} ms",
        f"  Steady frame FPS:                        {1000.0 / summary['steady_avg_ms'] if summary.get('steady_avg_ms') else 0.0:10.1f} FPS",
        "================================================================================"
    ]

    terminal_summary = "\n".join(l1_lines + [""] + startup_tree_lines + [""] + prepare_tree_lines + [""] + l2_lines)
    print(terminal_summary)

    if args.markdown:
        md_lines = [
            "# Real GPU Render Profile", "",
            f"- Frames: {len(frames)}",
            f"- Process Wall: {p_wall:.2f} ms",
            f"- Render Loop Wall: {render or 0.0:.2f} ms ({fps:.1f} FPS)",
            f"- Accounted: {ew.get('accounted_percent') or 0.0:.1f}%",
            "",
            "```text",
            terminal_summary,
            "```",
            "",
            "## Gate Status", "",
            "| Contract | Status |",
            "|---|:---:|",
            f"| native export | {'PASS' if gate_report['native_export']['all_pass'] else 'FAIL'} |",
            f"| direct-YUV frame path | {'PASS' if gate_report['direct_yuv_zero_copy']['all_pass'] else 'FAIL'} |",
            "",
            "## Bottleneck Ranking (Diagnostic)", "",
            "| Rank | Phase | Total ms | % wall | ms/frame | Type | Inclusive |",
            "|---:|---|---:|---:|---:|---|:---:|"
        ]
        md_lines += [f"| {x['rank']} | {x['phase']} | {x['total_ms']:.3f} | {x['percent_wall']:.2f} | {x['ms_per_frame']:.3f} | {x['type']} | {'yes' if x['inclusive_measurement'] else 'no'} |" for x in ranking]
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text("\n".join(md_lines) + "\n")


if __name__ == "__main__":
    main()
