#!/usr/bin/env python3
"""
tools/pilot_metrics.py — standalone JSONL pilot log summarizer.

Created 2026-07-12 — Test 16 / First-Principles Product Check #16.

Honest framing (per AGENTS.md §honesty):
    - This script is a STANDALONE helper. It has no PyYAML / pandas / numpy dependency.
    - It reads ~/.chronon3d/pilot/pilot.jsonl (path overridable via --log) and emits a
      summary suitable for the dashboard to consume (or for human reading).
    - The companion bash driver (tools/run_pilot.sh) handles render+log+summary via the
      same schema; this Python helper exists for the dashboard wire-up (forward-point:
      TICKET-PILOT-IG-DASHBOARD-WIREUP) and for ad-hoc inspection.

Usage:
    python3 tools/pilot_metrics.py --log ~/.chronon3d/pilot/pilot.jsonl --json
    python3 tools/pilot_metrics.py --log ~/.chronon3d/pilot/pilot.jsonl --table
    python3 tools/pilot_metrics.py --log ~/.chronon3d/pilot/pilot.jsonl --row my-shot-01
"""
from __future__ import annotations
import argparse, json, sys
from pathlib import Path
from typing import Any, Iterable


def _load_rows(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.exists():
        return rows
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError as e:
            print(f"[WARN] pilot_metrics: skipping malformed JSONL line: {e}", file=sys.stderr)
    return rows


def _median(xs: Iterable[float]) -> float | None:
    xs = sorted(xs)
    n = len(xs)
    if n == 0:
        return None
    if n % 2:
        return float(xs[n // 2])
    return float((xs[n // 2 - 1] + xs[n // 2]) / 2)


def summary(rows: list[dict[str, Any]]) -> dict[str, Any]:
    n_total = len(rows)
    n_posted = sum(1 for r in rows if r.get("video_posted"))
    n_discarded = sum(1 for r in rows if r.get("discarded"))
    n_bugs = sum(1 for r in rows if r.get("bugs"))
    mc = [int(r["manual_corrections"]) for r in rows
          if isinstance(r.get("manual_corrections"), (int, float))]
    ts = [float(r["time_saved_vs_baseline_min"]) for r in rows
          if isinstance(r.get("time_saved_vs_baseline_min"), (int, float))]
    dur = [float(r["duration_s"]) for r in rows
           if isinstance(r.get("duration_s"), (int, float))]
    r_ms = [float(r["render_ms"]) for r in rows
            if isinstance(r.get("render_ms"), (int, float))]
    posted_rate = round(100 * n_posted / n_total, 1) if n_total else 0.0
    return {
        "total_rows":            n_total,
        "video_posted_count":    n_posted,
        "discarded_count":       n_discarded,
        "bugs_count":            n_bugs,
        "manual_corrections_median": _median(mc),
        "time_saved_min_median": _median(ts),
        "video_duration_s_median": _median(dur),
        "render_ms_median":      _median(r_ms),
        "video_post_rate_pct":   posted_rate,
    }


def print_table(summary_rows: list[dict[str, Any]]) -> None:
    """Print the full pilot log as a fixed-width table (no pandas dep)."""
    if not summary_rows:
        print("[INFO] pilot_metrics: 0 rows in log")
        return
    cols = ["slug", "timestamp_iso", "day_index", "duration_s",
            "video_posted", "discarded", "manual_corrections", "time_saved_min", "bugs"]
    widths = {c: max(len(c), max((len(str(r.get(c, ""))) for r in summary_rows), default=0)) for c in cols}
    header = "  ".join(f"{c:<{widths[c]}}" for c in cols)
    print(header)
    print("  ".join("-" * widths[c] for c in cols))
    for r in summary_rows:
        print("  ".join(f"{str(r.get(c, '')):<{widths[c]}}" for c in cols))


def main() -> int:
    p = argparse.ArgumentParser(description="7-day pilot JSONL log summarizer")
    p.add_argument("--log", type=Path,
                   default=Path.home() / ".chronon3d" / "pilot" / "pilot.jsonl",
                   help="Path to pilot JSONL log (default: ~/.chronon3d/pilot/pilot.jsonl)")
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--json",  action="store_true", help="emit aggregate JSON")
    mode.add_argument("--table", action="store_true", help="emit full log table (stdout)")
    mode.add_argument("--row",   metavar="SLUG", help="emit one row by slug as JSON")
    args = p.parse_args()

    rows = _load_rows(args.log)
    agg = summary(rows)

    if args.json:
        print(json.dumps(agg, ensure_ascii=False, indent=2))
    elif args.table:
        print_table(rows)
    elif args.row:
        match = next((r for r in rows if r.get("slug") == args.row), None)
        if match is None:
            print(f"[FAIL] pilot_metrics: slug={args.row!r} not found in {args.log}",
                  file=sys.stderr)
            return 1
        print(json.dumps(match, ensure_ascii=False, indent=2))
    else:
        # Default: human-friendly aggregate like run_pilot.sh summary
        print(f"=== pilot_metrics summary ({args.log}) ===")
        for k, v in agg.items():
            print(f"  {k:33s} = {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
