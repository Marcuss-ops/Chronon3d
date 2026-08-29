#!/usr/bin/env python3
"""Compare two real GPU render profile JSON files."""
import argparse
import json
from pathlib import Path


def get(data, path):
    value = data
    for key in path.split("."):
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return value


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline", type=Path)
    ap.add_argument("new", type=Path)
    ap.add_argument("--json", type=Path, required=True)
    ap.add_argument("--markdown", type=Path, required=True)
    args = ap.parse_args()
    base, new = json.loads(args.baseline.read_text()), json.loads(args.new.read_text())
    metrics = {
        "wall_ms": "wall_ms",
        "render_loop_ms": "end_to_end.render_loop_wall_ms",
        "graph_execute_ms": "render_graph_cpu.graph_execute_ms.total_ms",
        "backend_overhead_ms": "render_graph_cpu.backend_overhead_ms.total_ms",
        "gpu_wait_cpu_ms": "gpu_vulkan_cuda.gpu_wait_cpu_ms",
        "frame_batch_drain_wait_us": "gpu_vulkan_cuda.frame_batch_drain_wait_us",
        "steady_state_avg_ms": "first_frame_vs_steady_state.steady_state_avg_ms",
        "vram_peak_mb": "hardware.metrics.vram_used_mb.peak",
    }
    rows = []
    for name, path in metrics.items():
        a, b = get(base, path), get(new, path)
        delta = (b - a) / a * 100.0 if (
            isinstance(a, (int, float)) and isinstance(b, (int, float)) and a
        ) else None
        rows.append({"metric": name, "baseline": a, "new": b, "delta_percent": delta,
                     "regression": bool(delta is not None and delta > 0.0)})
    result = {"schema": "chronon3d.real_gpu_profile_compare.v1",
              "baseline": str(args.baseline), "new": str(args.new), "rows": rows,
              "accepted": not any(row["regression"] for row in rows[:7])}
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(result, indent=2) + "\n")
    lines = ["# Real GPU profile comparison", "", "| Metric | Baseline | New | Delta % | Regression? |", "|---|---:|---:|---:|:---:|"]
    for row in rows:
        fmt = lambda x: "null" if x is None else f"{x:.3f}"
        delta = "null" if row["delta_percent"] is None else f"{row['delta_percent']:+.2f}%"
        lines.append(f"| {row['metric']} | {fmt(row['baseline'])} | {fmt(row['new'])} | {delta} | {'YES' if row['regression'] else 'no'} |")
    lines += ["", f"**Accepted:** {'yes' if result['accepted'] else 'no — experiment rejected'}"]
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    args.markdown.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
