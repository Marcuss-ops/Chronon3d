#!/usr/bin/env python3
"""
PR 0: Performance Benchmark Comparison Tool
============================================
Compares key telemetry metrics between two render runs.

Supports both:
  - Live SQLite telemetry DB queries (by run_id)
  - Saved JSON report files (offline comparison, no DB needed)

Usage:
    # Compare two runs by run_id (from telemetry DB)
    ./tools/perf/compare_telemetry.py <run_id_a> <run_id_b>

    # Compare with labels
    ./tools/perf/compare_telemetry.py <run_id_a>:label_a <run_id_b>:label_b

    # Compare two saved JSON report files
    ./tools/perf/compare_telemetry.py reports/perf/breathing_v14.json reports/perf/breathing_current.json

    # Compare a JSON file against a DB run_id
    ./tools/perf/compare_telemetry.py reports/perf/breathing_v14.json run_abc123:current

    # Auto-detect latest two runs
    ./tools/perf/compare_telemetry.py --latest-two

    # Specify a different telemetry DB
    ./tools/perf/compare_telemetry.py --db path/to/telemetry.db <run_id_a> <run_id_b>

JSON Report Formats:
    The script accepts two JSON formats:
    1. Comparison format (from --json-out): {"baseline":{"metrics":{...}}, "current":{"metrics":{...}}}
    2. Raw render_runs dump (from run_breathing_benchmark.sh): {"effective_fps":32.66, "_counters":{...}}

Key Metrics Compared (15 KPIs):
    effective_fps              — Effective frames per second
    e2e_wall_time_ms           — End-to-end wall time
    chronon_render_ms          — Pure render time (render_ms from DB)
    frame_conversion_copy_ms   — Frame conversion + copy time
    clearnode_restore_ms       — ClearNode restore time
    clearnode_clear_ms         — ClearNode clear time
    image_layer_multi_ms       — ImageLayerMulti node average time
    composite_ms               — Composite total time
    transform_ms               — Transform total time
    peak_memory_mb             — Peak allocated bytes
    framebuffer_peak_mb        — Peak framebuffer bytes
    cpu_efficiency_pct         — CPU efficiency (render time / (wall * cores))
    frames_graph_executed      — Frames where graph was executed
    frames_graph_skipped       — Frames where graph was skipped
    nodes_executed             — Total nodes executed

Pass/Fail Criteria:
    A run "passes" if it improves >= 2 of the primary metrics without
    regressing any critical metric beyond threshold.  Critical metrics
    are: effective_fps, peak_memory_mb, and e2e_wall_time_ms.
"""

import sqlite3
import sys
import os
import json
from pathlib import Path
from typing import Optional, Dict, Any, Tuple, List, Union


# ── Telemetry DB resolution ───────────────────────────────────────────────────
def find_telemetry_db() -> Optional[Path]:
    """Find the telemetry SQLite database."""
    project_dir = Path(__file__).resolve().parent.parent.parent

    candidates = [
        Path.home() / ".chronon3d" / "telemetry",  # default SQLite telemetry store
        project_dir / "output" / "telemetry.db",    # legacy / user-provided
    ]

    for candidate in candidates:
        if candidate.is_file():
            return candidate
        if candidate.is_dir():
            dbs = sorted(candidate.glob("*.sqlite"), key=os.path.getmtime, reverse=True)
            if dbs:
                return dbs[0]

    return None


def is_json_file(arg: str) -> bool:
    """Heuristic: detect if an argument is a JSON file path rather than a run_id."""
    p = Path(arg)
    return p.suffix == ".json" and p.exists()


# ── JSON report loading (offline comparison) ──────────────────────────────────
def load_metrics_from_json(path: str) -> Tuple[Dict[str, Any], str]:
    """Load metrics and label from a saved JSON report file.

    Supports two formats:
    1. Comparison format (from --json-out):
       {"baseline": {"label":..., "metrics": {...}}, "current": {"label":..., "metrics": {...}}}
       Uses the "current" metrics by default.
    2. Raw render_runs dump (from run_breathing_benchmark.sh):
       {"effective_fps": 32.66, "wall_time_ms": 4590, "_counters": {...}}
       Maps render_runs fields to our canonical metric keys.

    Returns:
        (metrics_dict, label_string)
        metrics_dict has the same keys as extract_run_metrics().
    """
    label = ""
    try:
        with open(path, "r") as f:
            data = json.load(f)
    except (json.JSONDecodeError, OSError) as e:
        print(f"Error: cannot read JSON file '{path}': {e}", file=sys.stderr)
        sys.exit(1)

    m: Dict[str, Any] = {}

    # Detect format
    if "baseline" in data and "current" in data:
        # Comparison format — use "current" metrics
        current = data.get("current", {})
        metrics = current.get("metrics", {})
        label = current.get("label", "")
        result = dict(metrics)

        # Validate that at least one expected key exists
        expected = {"effective_fps", "e2e_wall_time_ms", "peak_memory_mb"}
        if not any(k in result for k in expected):
            print(f"Warning: comparison JSON '{path}' lacks expected metric keys "
                  f"(none of {expected} found). Results may be zeros.", file=sys.stderr)
        return result, label

    # Raw render_runs dump format — map fields
    m["effective_fps"] = float(data.get("effective_fps", 0))
    m["e2e_wall_time_ms"] = float(data.get("wall_time_ms", 0))
    m["chronon_render_ms"] = float(data.get("render_ms", 0))
    m["cores"] = int(data.get("cores", 1))

    # Memory
    bytes_peak = int(data.get("bytes_allocated_peak", 0))
    m["peak_memory_mb"] = bytes_peak / (1024 * 1024) if bytes_peak else 0.0
    fb_peak = int(data.get("framebuffer_bytes_peak", 0))
    m["framebuffer_peak_mb"] = fb_peak / (1024 * 1024) if fb_peak else 0.0

    # Counters (from _counters sub-object if present)
    counters: Dict[str, Any] = data.get("_counters", {})

    m["frame_conversion_copy_ms"] = counters.get("frame_conversion_copy_ms", 0)
    m["clearnode_restore_ms"] = counters.get("clearnode_restore_ms", 0)
    m["clearnode_clear_ms"] = counters.get("clearnode_clear_ms", 0)
    m["clearnode_ms"] = counters.get("clearnode_ms", 0)
    m["composite_calls"] = counters.get("composite_calls", 0)
    m["composite_pixels"] = counters.get("composite_pixels", 0)
    m["transform_calls"] = counters.get("transform_calls", 0)
    m["transform_pixels"] = counters.get("transform_pixels", 0)
    m["nodes_executed"] = counters.get("nodes_executed", 0)
    m["graph_executed_frames"] = counters.get("graph_executed_frames", 0)
    m["graph_skipped_frames"] = counters.get("graph_skipped_frames", 0)
    m["framebuffer_pool_current_bytes"] = counters.get("framebuffer_pool_current_bytes", 0)
    m["image_layer_multi_ms"] = counters.get("image_layer_multi_ms", 0)
    m["composite_ms"] = counters.get("composite_ms", 0)
    m["transform_ms"] = counters.get("transform_ms", 0)
    m["source_node_ms"] = counters.get("source_node_ms", 0)
    m["avg_frame_ms"] = counters.get("avg_frame_ms", 0)
    m["frame_count"] = int(data.get("frames_total", 0) or data.get("frames_written", 0))

    # CPU Efficiency
    wall_ms = m["e2e_wall_time_ms"]
    render_ms = m["chronon_render_ms"]
    cores = m["cores"]
    if wall_ms > 0 and cores > 0:
        m["cpu_efficiency_pct"] = (render_ms / (wall_ms * cores)) * 100.0
    else:
        m["cpu_efficiency_pct"] = 0.0

    # Build label from raw dump
    comp = data.get("composition_id", "")
    commit = data.get("git_commit_short", "")
    if comp:
        if len(comp) > 30:
            comp = comp[:27] + "..."
        if commit and len(commit) > 8:
            commit = commit[:8]
        label = f"{comp} [{commit}]" if commit else comp

    # Fallback label: use filename
    if not label:
        stem = Path(path).stem
        for prefix in ["breathing_", "perf_", "bench_"]:
            if stem.startswith(prefix):
                label = stem[len(prefix):]
                break
        else:
            label = stem

    return m, label


# ── Metric extraction (DB) ────────────────────────────────────────────────────
def extract_run_metrics(db: sqlite3.Connection, run_id: str) -> Dict[str, Any]:
    """Extract all 15 key metrics for a given run_id from the SQLite DB."""
    m: Dict[str, Any] = {}

    # --- render_runs table ---
    try:
        row = db.execute(
            "SELECT effective_fps, wall_time_ms, render_ms, bytes_allocated_peak, "
            "framebuffer_bytes_peak, cores "
            "FROM render_runs WHERE run_id = ?",
            (run_id,),
        ).fetchone()
    except sqlite3.OperationalError as e:
        print(f"Error querying render_runs: {e}", file=sys.stderr)
        row = None

    if row:
        m["effective_fps"] = row[0] or 0.0
        m["e2e_wall_time_ms"] = row[1] or 0.0
        m["chronon_render_ms"] = row[2] or 0.0
        m["peak_memory_mb"] = (row[3] or 0) / (1024 * 1024) if row[3] else 0.0
        m["framebuffer_peak_mb"] = (row[4] or 0) / (1024 * 1024) if row[4] else 0.0
        m["cores"] = row[5] or 1
    else:
        m["effective_fps"] = 0.0
        m["e2e_wall_time_ms"] = 0.0
        m["chronon_render_ms"] = 0.0
        m["peak_memory_mb"] = 0.0
        m["framebuffer_peak_mb"] = 0.0
        m["cores"] = 1

    # --- render_counters table ---
    counter_map: Dict[str, int] = {}
    try:
        for crow in db.execute(
            "SELECT counter_name, counter_value FROM render_counters WHERE run_id = ?",
            (run_id,),
        ):
            counter_map[crow[0]] = crow[1]
    except sqlite3.OperationalError:
        print("Warning: render_counters table not found — counter metrics will be zero", file=sys.stderr)

    m["frame_conversion_copy_ms"] = counter_map.get("frame_conversion_copy_ms", 0)
    m["clearnode_restore_ms"] = counter_map.get("clearnode_restore_ms", 0)
    m["clearnode_clear_ms"] = counter_map.get("clearnode_clear_ms", 0)
    m["clearnode_ms"] = counter_map.get("clearnode_ms", 0)
    m["composite_calls"] = counter_map.get("composite_calls", 0)
    m["composite_pixels"] = counter_map.get("composite_pixels", 0)
    m["transform_calls"] = counter_map.get("transform_calls", 0)
    m["transform_pixels"] = counter_map.get("transform_pixels", 0)
    m["nodes_executed"] = counter_map.get("nodes_executed", 0)
    m["graph_executed_frames"] = counter_map.get("graph_executed_frames", 0)
    m["graph_skipped_frames"] = counter_map.get("graph_skipped_frames", 0)
    m["framebuffer_pool_current_bytes"] = counter_map.get("framebuffer_pool_current_bytes", 0)

    # --- Per-node aggregation for image_layer_multi, composite, transform ---
    try:
        node_rows = db.execute(
            "SELECT node_type, SUM(duration_ms) as total_ms, COUNT(*) as cnt "
            "FROM render_node_events WHERE run_id = ? "
            "GROUP BY node_type",
            (run_id,),
        ).fetchall()

        node_totals: Dict[str, float] = {}
        for nrow in node_rows:
            node_totals[nrow[0]] = nrow[1] if nrow[1] else 0.0

        m["composite_ms"] = node_totals.get("Composite", 0.0)
        m["transform_ms"] = node_totals.get("Transform", 0.0)
        m["source_node_ms"] = node_totals.get("Source", 0.0)

        img_rows = db.execute(
            "SELECT SUM(duration_ms) FROM render_node_events "
            "WHERE run_id = ? AND node_name LIKE '%image%'",
            (run_id,),
        ).fetchone()
        m["image_layer_multi_ms"] = img_rows[0] if img_rows and img_rows[0] else 0.0
    except sqlite3.OperationalError:
        m["composite_ms"] = 0.0
        m["transform_ms"] = 0.0
        m["source_node_ms"] = 0.0
        m["image_layer_multi_ms"] = 0.0

    # --- CPU Efficiency ---
    wall_ms = m["e2e_wall_time_ms"]
    render_ms = m["chronon_render_ms"]
    cores = m["cores"]
    if wall_ms > 0 and cores > 0:
        m["cpu_efficiency_pct"] = (render_ms / (wall_ms * cores)) * 100.0
    else:
        m["cpu_efficiency_pct"] = 0.0

    # --- Per-frame metrics ---
    try:
        frame_rows = db.execute(
            "SELECT AVG(duration_ms), COUNT(*) FROM render_frames WHERE run_id = ?",
            (run_id,),
        ).fetchone()
    except sqlite3.OperationalError:
        frame_rows = None
    if frame_rows:
        m["avg_frame_ms"] = frame_rows[0] or 0.0
        m["frame_count"] = frame_rows[1] or 0

    try:
        fr = db.execute(
            "SELECT frames_total FROM render_runs WHERE run_id = ?",
            (run_id,),
        ).fetchone()
    except sqlite3.OperationalError:
        fr = None
    if fr and fr[0]:
        m["frame_count"] = max(m.get("frame_count", 0), int(fr[0]))

    return m


def extract_run_label(db: sqlite3.Connection, run_id: str) -> str:
    """Build a human-readable label from the render_runs table."""
    try:
        row = db.execute(
            "SELECT composition_id, git_commit_short, build_type, "
            "finished_at_iso FROM render_runs WHERE run_id = ?",
            (run_id,),
        ).fetchone()
        if row:
            comp = row[0] or "unknown"
            commit = row[1] or "unknown"
            if len(comp) > 30:
                comp = comp[:27] + "..."
            if len(commit) > 8:
                commit = commit[:8]
            return f"{comp} [{commit}]"
    except sqlite3.OperationalError:
        pass
    return run_id[:16] + "..." if len(run_id) > 16 else run_id


def get_latest_runs(db: sqlite3.Connection, n: int = 2) -> List[str]:
    """Get the N most recent run_ids."""
    rows = db.execute(
        "SELECT run_id FROM render_runs ORDER BY finished_at_iso DESC LIMIT ?",
        (n,),
    ).fetchall()
    return [r[0] for r in rows]


# ── Formatting helpers ────────────────────────────────────────────────────────
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
BOLD = "\033[1m"
RESET = "\033[0m"


def fmt_ms(val: float) -> str:
    if val >= 1000:
        return f"{val / 1000:.2f}s"
    if val >= 1:
        return f"{val:.2f}ms"
    return f"{val * 1000:.2f}us"


def fmt_mb(val: float) -> str:
    if val >= 1024:
        return f"{val / 1024:.1f}GB"
    return f"{val:.1f}MB"


def fmt_pct(val: float) -> str:
    return f"{val:.1f}%"


def fmt_int(val: int) -> str:
    if val >= 1_000_000:
        return f"{val / 1_000_000:.1f}M"
    if val >= 1_000:
        return f"{val / 1_000:.1f}K"
    return str(val)


def delta_str(baseline: float, current: float, lower_is_better: bool = True) -> str:
    """Format delta percentage between baseline and current."""
    if baseline == 0:
        return "-"
    pct = ((current - baseline) / baseline) * 100.0
    improved = (pct < 0) if lower_is_better else (pct > 0)
    color = GREEN if improved else (RED if abs(pct) > 1 else "")
    return f"{color}{pct:+.1f}%{RESET}"


# ── Primary comparison function ───────────────────────────────────────────────
def compare_runs(
    run_a: str,
    label_a: str,
    run_b: str,
    label_b: str,
    threshold_pct: float = 1.0,
    quiet: bool = False,
    db: Optional[sqlite3.Connection] = None,
    metrics_a: Optional[Dict[str, Any]] = None,
    metrics_b: Optional[Dict[str, Any]] = None,
) -> int:
    """
    Compare two runs and return:
      0 — improvement (>=2 KPIs improved, no critical regressions)
      1 — no significant change or insufficient data
      2 — regression (critical metric degraded)

    Metrics can be provided directly (metrics_a/metrics_b for JSON sources)
    or extracted from the DB (db + run_a/run_b for DB sources).
    """
    if metrics_a is not None and metrics_b is not None:
        ma, mb = metrics_a, metrics_b
    elif db is not None:
        ma = extract_run_metrics(db, run_a)
        mb = extract_run_metrics(db, run_b)
    else:
        print("Error: no metrics source (provide db or metrics_a/metrics_b)", file=sys.stderr)
        return 1

    if not quiet:
        print()
        print(f"{'=' * 80}")
        print(f"  Performance Benchmark Comparison")
        print(f"{'=' * 80}")
        print(f"  Baseline:  {label_a}  (source: {run_a})")
        print(f"  Current:   {label_b}  (source: {run_b})")
        print(f"{'=' * 80}")
        print()

    # ── Define the 15 key metrics ─────────────────────────────────────────
    metrics_table: List[Tuple[str, str, str, bool]] = [
        ("effective_fps",           "Effective FPS",              "fps",  False),
        ("e2e_wall_time_ms",        "E2E Wall Time",              "ms",   True),
        ("chronon_render_ms",       "Chronon Render Time",        "ms",   True),
        ("frame_conversion_copy_ms","Frame Conversion & Copy",    "ms",   True),
        ("clearnode_restore_ms",    "ClearNode Restore",          "ms",   True),
        ("clearnode_clear_ms",      "ClearNode Clear",            "ms",   True),
        ("image_layer_multi_ms",    "Image Layer Multi",          "ms",   True),
        ("composite_ms",            "Composite Total",            "ms",   True),
        ("transform_ms",            "Transform Total",            "ms",   True),
        ("peak_memory_mb",          "Peak Memory",                "mb",   True),
        ("framebuffer_peak_mb",     "Framebuffer Peak",           "mb",   True),
        ("cpu_efficiency_pct",      "CPU Efficiency",             "pct",  False),
        ("graph_executed_frames",   "Frames Graph Executed",      "int",  True),
        ("graph_skipped_frames",    "Frames Graph Skipped",       "int",  False),
        ("nodes_executed",          "Nodes Executed",             "int",  True),
    ]

    if not quiet:
        header = f"| {'Metric':30s} | {'Baseline':>14s} | {'Current':>14s} | {'Delta':>10s} | Status |"
        print(header)
        print("|" + "-" * 31 + "|" + "-" * 16 + "|" + "-" * 16 + "|" + "-" * 12 + "|" + "-" * 8 + "|")

    improvements = 0
    regressions = 0
    critical_regression = False

    for key, name, unit, lower_better in metrics_table:
        a = ma.get(key, 0)
        b = mb.get(key, 0)

        if unit == "ms":
            fa, fb = fmt_ms(a), fmt_ms(b)
        elif unit == "mb":
            fa, fb = fmt_mb(a), fmt_mb(b)
        elif unit == "pct":
            fa, fb = fmt_pct(a), fmt_pct(b)
        elif unit == "fps":
            fa, fb = f"{a:.2f}", f"{b:.2f}"
        else:
            fa, fb = fmt_int(int(a)), fmt_int(int(b))

        if a == 0 and b == 0:
            delta = "0.0%"
            status = "-"
        elif a == 0:
            delta = "N/A"
            status = "NEW"
        else:
            pct = ((b - a) / a) * 100.0
            improved = (pct < 0) if lower_better else (pct > 0)
            is_significant = abs(pct) >= threshold_pct

            if improved and is_significant:
                delta = f"{GREEN}{pct:+.1f}%{RESET}"
                status = f"{GREEN}IMPROVED{RESET}"
                improvements += 1
            elif not improved and is_significant:
                delta = f"{RED}{pct:+.1f}%{RESET}"
                status = f"{RED}REGRESSED{RESET}"
                regressions += 1
                if key in ("effective_fps", "e2e_wall_time_ms", "peak_memory_mb"):
                    critical_regression = True
            else:
                delta = f"{pct:+.1f}%"
                status = "stable"

        if not quiet:
            print(f"| {name:30s} | {fa:>14s} | {fb:>14s} | {delta:>10s} | {status} |")

    if not quiet:
        print()

    # ── Secondary metrics ─────────────────────────────────────────────────
    secondary_metrics = [
        ("clearnode_ms",            "ClearNode Total",         "ms"),
        ("framebuffer_pool_current_bytes", "FB Pool Current",  "mb"),
        ("composite_calls",         "Composite Calls",         "int"),
        ("composite_pixels",        "Composite Pixels",        "int"),
        ("transform_calls",         "Transform Calls",         "int"),
        ("transform_pixels",        "Transform Pixels",        "int"),
    ]

    if any(ma.get(k, 0) or mb.get(k, 0) for k, _, _ in secondary_metrics) and not quiet:
        print(f"  Secondary Metrics:")
        print(f"  {'-' * 70}")
        for key, name, unit in secondary_metrics:
            a = ma.get(key, 0)
            b = mb.get(key, 0)
            if unit == "ms":
                fa, fb = fmt_ms(a), fmt_ms(b)
            elif unit == "mb":
                fa, fb = fmt_mb(a), fmt_mb(b)
            else:
                fa, fb = fmt_int(int(a)), fmt_int(int(b))
            ds = delta_str(a, b, lower_is_better=True)
            print(f"  {name:25s}  {fa:>12s} -> {fb:>12s}  ({ds})")
        print()

    # ── Verdict ───────────────────────────────────────────────────────────
    if not quiet:
        print(f"{'=' * 80}")
        print(f"  Verdict")
        print(f"{'=' * 80}")
    print(f"  Metrics improved:   {improvements}")
    print(f"  Metrics regressed:  {regressions}")
    if not quiet:
        print(f"  Critical regression: {'YES !' if critical_regression else 'No'}")

    if critical_regression:
        print(f"\n  {RED}X FAIL{RESET} - Critical metric regressed beyond {threshold_pct}%")
        print(f"    Do NOT merge. Critical metrics are: effective_fps,")
        print(f"    e2e_wall_time_ms, peak_memory_mb.")
        exit_code = 2
    elif improvements >= 2 and regressions == 0:
        print(f"\n  {GREEN}V PASS{RESET} - {improvements} metrics improved, no regressions")
        exit_code = 0
    elif improvements >= 3:
        print(f"\n  {YELLOW}! CONDITIONAL{RESET} - {improvements} improved, {regressions} regressed")
        print(f"    Review regressions to decide.")
        exit_code = 1
    else:
        print(f"\n  {YELLOW}! INCONCLUSIVE{RESET} - Less than 3 metrics improved")
        print(f"    Review the full report before merging.")
        exit_code = 1

    if not quiet:
        print(f"{'=' * 80}")
        print()
    return exit_code


# ── JSON report generation ────────────────────────────────────────────────────
def save_comparison_json(
    run_a: str, label_a: str,
    run_b: str, label_b: str,
    output_path: str,
    db: Optional[sqlite3.Connection] = None,
    metrics_a: Optional[Dict[str, Any]] = None,
    metrics_b: Optional[Dict[str, Any]] = None,
) -> None:
    """Save a structured JSON comparison report (works with DB or pre-loaded metrics)."""
    if metrics_a is not None and metrics_b is not None:
        ma, mb = metrics_a, metrics_b
    elif db is not None:
        ma = extract_run_metrics(db, run_a)
        mb = extract_run_metrics(db, run_b)
    else:
        print("Error: no metrics source for JSON output", file=sys.stderr)
        return

    report = {
        "schema": "chronon3d.perf.compare.v1",
        "baseline": {"label": label_a, "run_id": run_a, "metrics": ma},
        "current": {"label": label_b, "run_id": run_b, "metrics": mb},
        "deltas": {},
    }

    for key in ma:
        if key in mb and ma[key] != 0:
            report["deltas"][key] = round(((mb[key] - ma[key]) / ma[key]) * 100.0, 2)

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w") as f:
        json.dump(report, f, indent=2, default=str)

    print(f"  JSON comparison saved to: {output_path}")


# ── CLI ───────────────────────────────────────────────────────────────────────
def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Compare Chronon3D telemetry runs for performance benchmarking",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("runs", nargs="*", help="Run IDs or JSON file paths (with optional :label)")
    parser.add_argument("--db", help="Path to telemetry SQLite database")
    parser.add_argument("--threshold", type=float, default=1.0,
                        help="Regression threshold in %% (default: 1.0)")
    parser.add_argument("--latest-two", action="store_true",
                        help="Compare the two most recent runs")
    parser.add_argument("--json-out", help="Save structured JSON comparison to file")
    parser.add_argument("--quiet", action="store_true",
                        help="Only output verdict (0=pass, 1=inconclusive, 2=fail)")

    args = parser.parse_args()

    # ── Resolve runs (JSON files or DB run_ids) ──────────────────────────────
    run_specs: List[Tuple[str, str, bool]] = []  # (source, label, is_json)

    if args.latest_two:
        db_path = args.db or find_telemetry_db()
        if not db_path:
            print("Error: cannot find telemetry database. Specify with --db", file=sys.stderr)
            sys.exit(1)
        conn = sqlite3.connect(str(db_path))
        latest = get_latest_runs(conn, 2)
        if len(latest) < 2:
            print("Error: need at least 2 runs in the database", file=sys.stderr)
            sys.exit(1)
        for rid in latest:
            label = extract_run_label(conn, rid)
            run_specs.append((rid, label, False))
        conn.close()
    else:
        if len(args.runs) < 2:
            print("Error: need at least 2 run_ids or JSON files to compare", file=sys.stderr)
            print("Usage: compare_telemetry.py <run_a> <run_b>", file=sys.stderr)
            print("       compare_telemetry.py <report_a.json> <report_b.json>", file=sys.stderr)
            print("       compare_telemetry.py <report.json> <run_id:label>", file=sys.stderr)
            sys.exit(1)

        for spec in args.runs[:2]:
            if ":" in spec and not spec.endswith(".json"):
                # run_id:label format (not a JSON file with colon in name)
                rid, label = spec.split(":", 1)
                run_specs.append((rid, label, False))
            elif is_json_file(spec):
                # Label extracted later by load_metrics_from_json()
                run_specs.append((spec, "", True))
            else:
                run_specs.append((spec, "", False))  # label resolved later from DB

    (src_a, label_a, is_json_a), (src_b, label_b, is_json_b) = run_specs[0], run_specs[1]

    # ── Connect to DB if needed ───────────────────────────────────────────
    conn: Optional[sqlite3.Connection] = None
    if not is_json_a or not is_json_b:
        db_path = args.db or find_telemetry_db()
        if not db_path:
            print("Error: cannot find telemetry database. Specify with --db for DB run_ids", file=sys.stderr)
            sys.exit(1)
        try:
            conn = sqlite3.connect(str(db_path))
        except sqlite3.Error as e:
            print(f"Error: cannot open database: {e}", file=sys.stderr)
            sys.exit(1)

    # ── Load metrics (pre-load BOTH sides for mixed JSON+DB comparison) ────
    metrics_a: Optional[Dict[str, Any]] = None
    metrics_b: Optional[Dict[str, Any]] = None

    if is_json_a:
        metrics_a, json_label_a = load_metrics_from_json(src_a)
        if not label_a:
            label_a = json_label_a
        src_a = Path(src_a).stem  # Use filename stem as source identifier
    else:
        if not label_a and conn:
            label_a = extract_run_label(conn, src_a)
        if conn:
            metrics_a = extract_run_metrics(conn, src_a)

    if is_json_b:
        metrics_b, json_label_b = load_metrics_from_json(src_b)
        if not label_b:
            label_b = json_label_b
        src_b = Path(src_b).stem  # Use filename stem as source identifier
    else:
        if not label_b and conn:
            label_b = extract_run_label(conn, src_b)
        if conn:
            metrics_b = extract_run_metrics(conn, src_b)

    run_a_effective = src_a
    run_b_effective = src_b

    # ── Compare (use pre-loaded metrics for both sides) ────────────────────
    exit_code = compare_runs(
        run_a_effective, label_a,
        run_b_effective, label_b,
        threshold_pct=args.threshold,
        quiet=args.quiet,
        metrics_a=metrics_a,
        metrics_b=metrics_b,
    )

    # ── Quiet mode: print compact verdict ──────────────────────────────────
    if args.quiet:
        verdicts = {0: "PASS", 1: "INCONCLUSIVE", 2: "FAIL"}
        print(f"{verdicts.get(exit_code, 'UNKNOWN')}: {label_a} -> {label_b}")
        if exit_code == 0:
            print("  No critical regressions, >=2 KPIs improved.")
        elif exit_code == 2:
            print("  Critical metric regressed - do NOT merge.")

    # ── Save JSON if requested (use pre-loaded metrics) ───────────────────
    if args.json_out:
        save_comparison_json(
            run_a_effective, label_a,
            run_b_effective, label_b,
            args.json_out,
            metrics_a=metrics_a,
            metrics_b=metrics_b,
        )

    if conn:
        conn.close()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
