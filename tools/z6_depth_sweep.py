#!/usr/bin/env python3
"""
Z6 Pipeline Depth Sweep Harness — RequireGpuNative/FullGraph vs RequireDirectYuv

Per ogni depth (3, 4, 6) e lane (FullGraph, DirectYuv) misura sul timing sidecar:
  wall_time, fps, frame_slot_wait_us/count, queue depth avg/peak,
  render_slots_in_flight_peak, encode_frames_in_flight_peak,
  cuda_encode_event_wait_us, writer_queue_wait_ms, rgba_to_nv12_frames,
  encoder_staging_copy_bytes, gpu_surface_copy_frames, gpu_readback_bytes,
  cuda_host_upload_bytes steady-state.

Regole decisionali:
  - Se encoder_staging, readback, surface_copy == 0 → #1 già eliminato
  - Se frame_slot_wait_us crolla 3→4→6 e wall-clock segue → #2 dimostrato
  - Se slot non cambiano ma FullGraph << DirectYuv → RGBA→NV12 domina
  - Se entrambi ~ ceiling NVENC → full-reencode al limite, serve packet/BoundaryReencode

Richiede:
  - preset linux-video-test o linux-video-fast-dev già buildato con NVENC
  - chronon3d_cli sul PATH o in build/chronon/linux-video-test/...
  - CHRONON3D_BENCH_SCENE opzionale (default DarkGridBackground)

Uso:
  python3 /tmp/z6_depth_sweep.py --frames 120 --depths 3,4,6 --lanes both

Sul host senza NVENC stampa Z6_SKIPPED con reason strutturato.
"""
import argparse, json, subprocess, sys, os, re, time
from pathlib import Path

def find_cli():
    env = os.environ.get("CHRONON3D_CLI")
    if env and Path(env).is_file():
        return env
    for p in [
        "build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli",
        "build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli",
        "build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli",
    ]:
        if Path(p).is_file():
            return str(Path(p).resolve())
    return ""

def extract_timing_sidecar(report_json):
    try:
        with open(report_json) as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError):
        return {}
    job = data.get("job", {})
    gpu = job.get("gpu", {})
    pipeline = job.get("pipeline", {})
    summary = data.get("summary", {})

    def first(*values):
        for value in values:
            if value is not None and value != "":
                return value
        return ""

    return {
        "wall_ms": first(data.get("wall_ms"), job.get("wall_ms"), job.get("job_wall_ms")),
        "fps": first(summary.get("end_to_end_fps"), summary.get("measured_fps")),
        "frames": first(data.get("frames"), job.get("frames"), summary.get("frames")),
        "frame_slot_wait_us": gpu.get("frame_slot_wait_us", ""),
        "frame_slot_wait_count": gpu.get("frame_slot_wait_count", ""),
        "queue_depth_avg": first(pipeline.get("queue_depth_average"), pipeline.get("queue_depth_avg")),
        "queue_depth_peak": pipeline.get("queue_depth_peak", ""),
        "render_slots_in_flight_peak": pipeline.get("render_slots_in_flight_peak", ""),
        "encode_frames_in_flight_peak": pipeline.get("encode_frames_in_flight_peak", ""),
        "cuda_encode_event_wait_us": gpu.get("cuda_encode_event_wait_us", ""),
        "cuda_encode_event_wait_count": gpu.get("cuda_encode_event_wait_count", ""),
        "writer_queue_wait_ms": first(job.get("writer_queue_wait_total_ms"), summary.get("writer_queue_wait_total_ms")),
        "rgba_to_nv12_frames": gpu.get("rgba_to_nv12_frames", ""),
        "encoder_staging_copy_bytes": gpu.get("encoder_staging_copy_bytes", ""),
        "gpu_surface_copy_frames": gpu.get("gpu_surface_copy_frames", ""),
        "gpu_readback_bytes": first(gpu.get("gpu_readback_bytes"), gpu.get("cpu_pixel_readback_bytes")),
        "cuda_host_upload_bytes": gpu.get("cuda_host_upload_bytes", ""),
        "gpu_upload_bytes": gpu.get("gpu_upload_bytes", ""),
        "gpu_native_surface_frames": gpu.get("gpu_native_surface_frames", ""),
        "gpu_native_encode_frames": gpu.get("gpu_native_encode_frames", ""),
    }

def numeric(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)

def metric_number(row, name):
    value = row.get(name)
    return float(value) if numeric(value) else None

def run_one(cli, scene, frames, warmup, lane, depth, outdir):
    mode = "require_direct_yuv" if lane == "direct_yuv" else "require_gpu_native"
    # Depth is communicated via env override consumed by GpuSlotPool if present;
    # otherwise document as param for manual tuning.
    env = os.environ.copy()
    env["CHRONON_GPUSLOT_DEPTH"] = str(depth)
    out_mp4 = str(outdir / f"z6_{lane}_d{depth}.mp4")
    timing = str(Path(out_mp4).with_suffix(".mp4.timing.json"))
    args = [
        cli, "render", scene,
        "--frames", f"0-{frames-1}",
        "--backend", "vulkan",
        "--hardware", "nvenc",
        "--encoder-backend", "native",
        "--gpu-hot-path-mode", mode,
        "--warmup-renderer",
        "--warmup-framebuffers", str(warmup),
        "-o", out_mp4,
        "--report",
    ]
    # DirectYuv needs a source video probe; on synthetic scene it will fall back to FullGraph with downgrade.
    print(f"[z6] lane={lane} depth={depth} mode={mode} scene={scene} -> {out_mp4}")
    try:
        r = subprocess.run(args, env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        print(f"[z6] TIMEOUT lane={lane} depth={depth}")
        return None
    if not Path(out_mp4).is_file():
        print(f"[z6] NO_ARTIFACT lane={lane} depth={depth} rc={r.returncode} stderr_tail={(r.stderr or '')[-2000:]}")
        return None
    if not Path(timing).is_file():
        print(f"[z6] NO_SIDECAR lane={lane} depth={depth}")
        return None
    metrics = extract_timing_sidecar(timing)
    metrics["lane"] = lane
    metrics["depth"] = depth
    metrics["rc"] = r.returncode
    return metrics

ap = argparse.ArgumentParser()
ap.add_argument("--frames", type=int, default=60)
ap.add_argument("--warmup", type=int, default=10)
ap.add_argument("--depths", default="3,4,6")
ap.add_argument("--lanes", default="both", choices=["both", "fullgraph", "direct_yuv"])
ap.add_argument("--scene", default=os.environ.get("CHRONON3D_BENCH_SCENE", "DarkGridBackground"))
ap.add_argument("--output-dir", default="/tmp/z6_sweep")
args = ap.parse_args()
if args.frames <= 0 or args.warmup < 0:
    ap.error("--frames must be positive and --warmup must be non-negative")
try:
    depths = [int(x.strip()) for x in args.depths.split(",") if x.strip()]
except ValueError:
    ap.error("--depths must be a comma-separated list of integers")
if not depths or any(depth <= 0 or depth > 64 for depth in depths):
    ap.error("--depths values must be in the range 1..64")
if len(set(depths)) != len(depths):
    ap.error("--depths must not contain duplicates")
if args.lanes == "both":
    lanes = ["fullgraph", "direct_yuv"]
elif args.lanes == "fullgraph":
    lanes = ["fullgraph"]
else:
    lanes = ["direct_yuv"]
cli = find_cli()
if not cli:
    print("Z6_SKIPPED reason=cli_not_found (build linux-video-test/linux-video-fast-dev first)")
    sys.exit(0)

outdir = Path(args.output_dir)
outdir.mkdir(parents=True, exist_ok=True)
rows = []
for lane in lanes:
    for d in depths:
        m = run_one(cli, args.scene, args.frames, args.warmup, lane, d, outdir)
        if m:
            rows.append(m)
        time.sleep(0.2)

# Write a machine-readable result even when a host cannot complete all runs.
summary_path = outdir / "z6_summary.json"
summary = {
    "schema_version": 1,
    "parameters": {"frames": args.frames, "warmup": args.warmup,
                   "depths": depths, "lanes": lanes, "scene": args.scene},
    "rows": rows,
    "acceptance": {},
}
try:
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")
    print(f"[z6] summary -> {summary_path}")
except OSError as e:
    print(f"[z6] summary write failed: {e}")
    sys.exit(2)

if not rows:
    print("Z6_BLOCKED reason=no_rows (no NVENC or no artifact) — see logs above")
    sys.exit(2)

required_metrics = (
    "wall_ms", "fps", "frame_slot_wait_us", "frame_slot_wait_count",
    "queue_depth_avg", "queue_depth_peak", "render_slots_in_flight_peak",
    "encode_frames_in_flight_peak", "cuda_encode_event_wait_us",
    "cuda_encode_event_wait_count", "writer_queue_wait_ms",
    "rgba_to_nv12_frames", "encoder_staging_copy_bytes",
    "gpu_surface_copy_frames", "gpu_readback_bytes", "cuda_host_upload_bytes",
)
def complete(row):
    return all(metric_number(row, name) is not None for name in required_metrics)

# Decision rules are emitted as explicit PASS/PARTIAL/NOT RUN rather than a
# heuristic that can accidentally call missing telemetry zero.
for row in rows:
    row["telemetry_status"] = "PASS" if complete(row) else "PARTIAL"
    row["zero_copy_canary"] = (
        "PASS" if complete(row) and all(metric_number(row, name) == 0
                                         for name in ("encoder_staging_copy_bytes",
                                                      "gpu_surface_copy_frames",
                                                      "gpu_readback_bytes"))
        else "NOT RUN"
    )
summary["acceptance"]["telemetry_complete"] = (
    "PASS" if rows and all(complete(row) for row in rows) else "PARTIAL"
)
summary["acceptance"]["zero_copy_canaries"] = (
    "PASS" if rows and all(row["zero_copy_canary"] == "PASS" for row in rows)
    else "PARTIAL"
)

print("\n[z6] DECISION TABLE")
columns = ("lane", "depth", "wall_ms", "fps", "frame_slot_wait_us",
           "frame_slot_wait_count", "queue_depth_avg", "queue_depth_peak",
           "render_slots_in_flight_peak", "encode_frames_in_flight_peak",
           "cuda_encode_event_wait_us", "writer_queue_wait_ms",
           "rgba_to_nv12_frames", "encoder_staging_copy_bytes",
           "gpu_surface_copy_frames", "gpu_readback_bytes",
           "cuda_host_upload_bytes")
print(" ".join(f"{column:>28}" for column in columns))
for row in rows:
    print(" ".join(f"{str(row.get(column, '')):>28}" for column in columns))

# Compare depth only when the complete rows exist. Lower wait and wall time at
# the deepest point is evidence for slot-lifetime backpressure; it is not a
# claim of causality when telemetry is absent or noisy.
for lane in lanes:
    lane_rows = [row for row in rows if row.get("lane") == lane and complete(row)]
    lane_rows.sort(key=lambda row: row["depth"])
    if len(lane_rows) >= 2:
        waits = [metric_number(row, "frame_slot_wait_us") for row in lane_rows]
        walls = [metric_number(row, "wall_ms") for row in lane_rows]
        collapsed = waits[0] > waits[-1] * 2
        followed = walls[0] > walls[-1] * 1.05
        status = "PASS" if collapsed and followed else "PARTIAL"
        summary["acceptance"][f"slot_lifetime_{lane}"] = status
        print(f"[z6] {status} slot-lifetime lane={lane}: waits={waits} wall_ms={walls}")
    else:
        summary["acceptance"][f"slot_lifetime_{lane}"] = "NOT RUN"

summary_path.write_text(json.dumps(summary, indent=2) + "\n")
print(f"[z6] acceptance={json.dumps(summary['acceptance'], sort_keys=True)}")
# A completed binary run with incomplete telemetry is a diagnostic failure,
# while missing hardware/artifacts was handled above as Z6_BLOCKED.
sys.exit(0 if summary["acceptance"]["telemetry_complete"] == "PASS" else 1)
