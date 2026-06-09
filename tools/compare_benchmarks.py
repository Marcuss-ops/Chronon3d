#!/usr/bin/env python3
"""
Compare benchmark results from 3 configurations stored in the telemetry SQLite database.

Usage:
    ./tools/compare_benchmarks.py <telemetry_db> <run_id_A> <run_id_B> <run_id_C>
    
    Or use labels:
    ./tools/compare_benchmarks.py <telemetry_db> <run_id_A:label_A> <run_id_B:label_B> <run_id_C:label_C>

Extracts the 6 key metrics:
  - avg active frame (ms)
  - p95 active frame (ms)
  - graph_total_ms
  - node_dispatch_ms
  - clearnode memcpy ms
  - peak memory (MB)

Requires: sqlite3 (standard library)
"""

import sqlite3
import sys
import math
from pathlib import Path


def extract_metrics(db: sqlite3.Connection, run_id: str) -> dict:
    """Extract the 6 key metrics for a given run_id from the telemetry DB."""
    metrics = {}

    # ── Per-frame timing: avg + p95 of active frames ────────────────────
    durations = []
    for row in db.execute(
        "SELECT duration_ms FROM render_frames WHERE run_id = ?",
        (run_id,),
    ):
        durations.append(row[0])

    if durations:
        durations.sort()
        n = len(durations)
        metrics["avg_active_frame_ms"] = sum(durations) / n
        # P95: 95th percentile
        idx = max(0, min(n - 1, int(math.ceil(0.95 * n) - 1)))
        metrics["p95_active_frame_ms"] = durations[idx]
    else:
        metrics["avg_active_frame_ms"] = 0.0
        metrics["p95_active_frame_ms"] = 0.0

    # ── Counter-based metrics (from render_counters table) ───────────────
    counter_map = {}
    for row in db.execute(
        "SELECT counter_name, counter_value FROM render_counters WHERE run_id = ?",
        (run_id,),
    ):
        counter_map[row[0]] = row[1]

    metrics["graph_total_ms"] = counter_map.get("graph_total_ms", 0)
    metrics["node_dispatch_ms"] = counter_map.get("node_dispatch_ms", 0)
    metrics["clearnode_memcpy_ms"] = counter_map.get("clearnode_memcpy_ms", 0)

    # ── Peak memory (from render_runs table) ─────────────────────────────
    row = db.execute(
        "SELECT framebuffer_bytes_peak, process_rss_peak_mb FROM render_runs WHERE run_id = ?",
        (run_id,),
    ).fetchone()
    if row:
        # framebuffer_bytes_peak is in bytes, convert to MB
        fb_peak_mb = row[0] / (1024 * 1024) if row[0] else 0.0
        rss_peak_mb = row[1] or 0.0
        metrics["peak_memory_fb_mb"] = fb_peak_mb
        metrics["peak_memory_rss_mb"] = rss_peak_mb
        metrics["peak_memory_mb"] = max(fb_peak_mb, rss_peak_mb)
    else:
        metrics["peak_memory_fb_mb"] = 0.0
        metrics["peak_memory_rss_mb"] = 0.0
        metrics["peak_memory_mb"] = 0.0

    return metrics


def fmt_ms(val: float) -> str:
    """Format milliseconds to a readable string."""
    if val >= 1000:
        return f"{val/1000:.2f}s"
    if val >= 1:
        return f"{val:.2f}ms"
    return f"{val*1000:.2f}us"


def fmt_mb(val: float) -> str:
    """Format megabytes."""
    if val >= 1024:
        return f"{val/1024:.1f}GB"
    return f"{val:.1f}MB"


def fmt_row(name: str, a: float, b: float, c: float, unit: str = "ms") -> str:
    """Format a comparison row, highlighting the best value."""
    vals = [a, b, c]
    # Determine best (lower is better for all metrics)
    best = min(vals)
    best_color = "\033[92m"  # green
    reset = "\033[0m"

    def cell(v: float) -> str:
        formatted = fmt_ms(v) if unit == "ms" else fmt_mb(v)
        if v == best:
            return f"{best_color}{formatted}{reset}"
        return formatted

    return f"| {name:30s} | {cell(a):>12s} | {cell(b):>12s} | {cell(c):>12s} |"


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        sys.exit(1)

    db_path = Path(sys.argv[1])
    if not db_path.exists():
        print(f"Error: telemetry database not found: {db_path}")
        sys.exit(1)

    # Parse run_ids with optional labels
    run_specs = []
    for i, arg in enumerate(sys.argv[2:5]):
        if ":" in arg:
            rid, label = arg.split(":", 1)
        else:
            rid, label = arg, f"Config-{chr(65+i)}"  # A, B, C
        run_specs.append((rid, label))

    try:
        conn = sqlite3.connect(str(db_path))
    except sqlite3.Error as e:
        print(f"Error: cannot open database: {e}")
        sys.exit(1)

    print(f"\n{'=' * 80}")
    print(f"  Benchmark Comparison")
    print(f"  Database: {db_path}")
    print(f"{'=' * 80}\n")

    # Extract metrics for each run
    all_metrics = []
    for rid, label in run_specs:
        m = extract_metrics(conn, rid)
        all_metrics.append((label, m))
        print(f"  {label}: run_id={rid}")
    print()

    conn.close()

    # ── Display comparison table ────────────────────────────────────────
    print(f"{'=' * 80}")
    print(f"  Key Metrics Comparison")
    print(f"{'=' * 80}")
    headers = ["Metric"] + [label for label, _ in all_metrics]
    header_row = "| {:30s} |".format(headers[0])
    for h in headers[1:]:
        header_row += f" {h:>12s} |"
    print(header_row)
    print("|" + "-" * 30 + "|" + "|".join(["-" * 14] * 3) + "|")

    l_a, m_a = all_metrics[0]
    l_b, m_b = all_metrics[1]
    l_c, m_c = all_metrics[2]

    rows = [
        ("avg active frame", m_a["avg_active_frame_ms"], m_b["avg_active_frame_ms"], m_c["avg_active_frame_ms"]),
        ("p95 active frame", m_a["p95_active_frame_ms"], m_b["p95_active_frame_ms"], m_c["p95_active_frame_ms"]),
        ("graph total", m_a["graph_total_ms"], m_b["graph_total_ms"], m_c["graph_total_ms"]),
        ("node dispatch", m_a["node_dispatch_ms"], m_b["node_dispatch_ms"], m_c["node_dispatch_ms"]),
        ("clearnode memcpy", m_a["clearnode_memcpy_ms"], m_b["clearnode_memcpy_ms"], m_c["clearnode_memcpy_ms"]),
        ("peak memory (max)", m_a["peak_memory_mb"], m_b["peak_memory_mb"], m_c["peak_memory_mb"]),
    ]

    for name, a, b, c in rows:
        unit = "mb" if "memory" in name or "RSS" in name else "ms"
        print(fmt_row(name, a, b, c, unit))

    print(f"\n{'=' * 80}")
    print(f"  Legend:  \033[92mgreen\033[0m = best value (lower is better)")
    print(f"{'=' * 80}\n")

    # ── Delta percentages relative to config A ───────────────────────────
    print(f"\n--- Deltas vs {l_a} (baseline) ---")
    for name, a, b, c in rows:
        def delta(base: float, cur: float) -> str:
            if base == 0:
                return "—"
            pct = ((cur - base) / base) * 100.0
            if pct >= 0:
                return f"+{pct:.1f}%"
            return f"{pct:.1f}%"
        print(f"  {name:30s}  {l_b}: {delta(a, b):>8s}    {l_c}: {delta(a, c):>8s}")


if __name__ == "__main__":
    main()
