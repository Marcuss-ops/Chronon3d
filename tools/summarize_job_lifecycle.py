#!/usr/bin/env python3
"""Summarize `[INFO] job-lifecycle:` phase timings emitted by Chronon3D.

Why this exists (2026-09-28): the daemon job lifecycle had an intermittent ~1 s
teardown that survived every fix made so far — the metrics-worker wait, the NVML
sampler and the named encoder/full-graph resets were all attributed and cleared
(docs/evidence/daemon-job-lifecycle-2026-09-16.md §5,
docs/evidence/nvml-sampler-teardown-2026-09-17.md §4). The remaining candidate
was the set of objects destroyed IMPLICITLY after the session's destructor body,
which no timer covered. `pipe_export_session.hpp` now resets and times them
(sink_ctx, device_runtime, device_reservation), and this tool turns a log stream
into the per-phase distribution that says WHICH phase owns the second.

Input: log files, directories (scanned for *.log/*.txt/*.jsonl), or '-' for
stdin. Every line containing `job-lifecycle:` is one record; every `key=value`
whose key ends in `_ms` is one phase measurement.

Exit codes:
  0: PASS — no phase over the flag threshold (or --fail-on-outlier not set)
  1: OUTLIER — at least one phase over the threshold and --fail-on-outlier set
  2: NO_INPUT — nothing parseable in the given inputs
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Iterable

# One `key=value` pair as measured in the lifecycle line. Keys are snake_case and
# always end in `_ms`; matching that suffix is what keeps unrelated pairs (e.g.
# `frames=121`) out of the phase table.
_PAIR = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*_ms)=(?:\"|')?(-?[0-9]+(?:\.[0-9]+)?)")


def parse_lifecycle_lines(lines: Iterable[str]) -> list[dict[str, float]]:
    """Return one dict of phase→ms per job-lifecycle line."""
    records: list[dict[str, float]] = []
    for line in lines:
        if "job-lifecycle:" not in line:
            continue
        record = {name: float(value) for name, value in _PAIR.findall(line)}
        if record:
            records.append(record)
    return records


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    k = (len(ordered) - 1) * pct
    low, high = int(k), min(int(k) + 1, len(ordered) - 1)
    if low == high:
        return ordered[low]
    return ordered[low] * (high - k) + ordered[high] * (k - low)


def summarize(
    records: list[dict[str, float]],
    flag_over: float,
    only: tuple[str, ...] = (),
) -> dict[str, Any]:
    """Aggregate per-phase stats and the records that break the threshold.

    `only` keeps phases whose name contains ANY of the given substrings. It
    exists because the lifecycle line mixes two kinds of number: the job's own
    magnitudes (`execute_ms` ~1000 ms on every job, by design) and the teardown
    phases a probe is looking at. Without the filter, a threshold that catches a
    teardown outlier would flag every ordinary job for its execute time.

    `spread` (max/median) is reported per phase because the anomaly this probe
    hunts is INTERMITTENT: the phase whose max dwarfs its median is the one that
    owns the second on one job in four, regardless of any absolute threshold.
    """
    def selected(name: str) -> bool:
        return not only or any(token in name for token in only)

    phases: dict[str, list[float]] = {}
    for record in records:
        for name, value in record.items():
            if selected(name):
                phases.setdefault(name, []).append(value)

    stats: dict[str, dict[str, float]] = {}
    for name, values in phases.items():
        median = percentile(values, 0.50)
        peak = max(values)
        stats[name] = {
            "jobs": float(len(values)),
            "median": median,
            "p95": percentile(values, 0.95),
            "max": peak,
            "mean": sum(values) / len(values),
            "spread": (peak / median) if median > 0 else 0.0,
        }

    outliers = []
    for index, record in enumerate(records):
        flagged = {name: value for name, value in record.items()
                   if selected(name) and value > flag_over}
        if flagged:
            outliers.append({
                "index": index,
                "total_ms": sum(record.values()),
                "phases": flagged,
            })
    return {
        "jobs": len(records),
        "flag_over_ms": flag_over,
        "only": list(only),
        "phases": stats,
        "outliers": outliers,
    }


def render_text(report: dict[str, Any]) -> str:
    scope = f"  only={','.join(report['only'])}" if report["only"] else ""
    lines = [
        f"job-lifecycle records: {report['jobs']}  (flag over {report['flag_over_ms']:.1f} ms)"
        f"{scope}",
        "",
        f"{'phase':<32}{'jobs':>6}{'median':>10}{'p95':>10}{'max':>10}{'mean':>10}{'spread':>9}",
    ]
    for name, stat in report["phases"].items():
        lines.append(
            f"{name:<32}{int(stat['jobs']):>6}{stat['median']:>10.1f}{stat['p95']:>10.1f}"
            f"{stat['max']:>10.1f}{stat['mean']:>10.1f}{stat['spread']:>9.1f}"
        )
    lines.append("")
    if not report["outliers"]:
        lines.append(f"no phase over the {report['flag_over_ms']:.1f} ms threshold")
        return "\n".join(lines)
    lines.append(f"{len(report['outliers'])} record(s) with a phase over the threshold:")
    for outlier in report["outliers"]:
        phases = " ".join(f"{name}={value:.1f}ms" for name, value in sorted(outlier["phases"].items()))
        lines.append(f"  record #{outlier['index']} total={outlier['total_ms']:.1f}ms {phases}")
    return "\n".join(lines)


def _iter_input_lines(inputs: list[str]) -> Iterable[str]:
    for raw in inputs:
        if raw == "-":
            yield from sys.stdin
            continue
        path = Path(raw)
        if path.is_dir():
            files = sorted(
                p for p in path.rglob("*")
                if p.is_file() and p.suffix in {".log", ".txt", ".jsonl"}
            )
            for file in files:
                with file.open("r", encoding="utf-8", errors="replace") as handle:
                    yield from handle
            continue
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            yield from handle


def self_test() -> int:
    """Prove the parser and the outlier detector on an inline fixture.

    The fixture mirrors the real production shape: three ordinary jobs whose
    teardown is ~100-200 ms and one job whose teardown is the intermittent
    ~1061 ms second — the case the probe must be able to point at.
    """
    def record(teardown: float, device_runtime: float) -> str:
        # Real lifecycle lines always print the same key set, so the fixture
        # does too: a phase missing from some records would make its median (and
        # therefore its spread) depend on which jobs happened to log it.
        return (
            "[INFO] job-lifecycle: execute_ms=1011.2 receipt_build_ms=310.4 "
            f"session_teardown_ms={teardown} slot_pool_close_ms=4.1 surface_release_ms=9.7 "
            "queue_close_ms=0.4 writer_join_ms=0.9 encoder_reset_ms=1.0 decoder_reset_ms=0.3 "
            "full_graph_reset_ms=0.2 sink_ctx_reset_ms=0.1 "
            f"device_runtime_reset_ms={device_runtime} device_reservation_reset_ms=0.1"
        )

    fixture = [record(124.2, 0.1), record(108.9, 0.1), record(131.4, 0.2), record(1061.3, 1059.4)]
    records = parse_lifecycle_lines(fixture)
    if len(records) != 4:
        print(f"self-test failed: parsed {len(records)} records, want 4", file=sys.stderr)
        return 1
    if "execute_ms" not in records[0] or records[0]["execute_ms"] != 1011.2:
        print(f"self-test failed: first record = {records[0]}", file=sys.stderr)
        return 1

    # The probe looks at teardown phases only: `execute_ms` (~1000 ms on every
    # job) is a normal magnitude, not an anomaly, and including it would make
    # every record an outlier.
    only = ("teardown_ms", "reset_ms", "close_ms", "join_ms")
    report = summarize(records, flag_over=500.0, only=only)
    if len(report["outliers"]) != 1:
        print(f"self-test failed: outliers = {report['outliers']}", file=sys.stderr)
        return 1
    outlier = report["outliers"][0]
    if outlier["index"] != 3 or "device_runtime_reset_ms" not in outlier["phases"]:
        print(f"self-test failed: outlier = {outlier}", file=sys.stderr)
        return 1
    # The intermittent second must be attributable to ONE named phase, which is
    # the whole point of the probe: the unattributed case would have no phase.
    if round(outlier["phases"]["device_runtime_reset_ms"], 1) != 1059.4:
        print(f"self-test failed: phase value = {outlier['phases']}", file=sys.stderr)
        return 1
    if report["phases"]["session_teardown_ms"]["max"] != 1061.3:
        print("self-test failed: session teardown max was not tracked", file=sys.stderr)
        return 1
    # The intermittency signature: a phase whose peak dwarfs its median. This is
    # what a reader uses when the absolute threshold is not the question.
    spread = report["phases"]["device_runtime_reset_ms"]["spread"]
    if spread < 100:
        print(f"self-test failed: device_runtime spread = {spread}, want the intermittent phase", file=sys.stderr)
        return 1
    print("self-test OK: 4 records parsed, the 1061.3 ms teardown attributed to "
          f"device_runtime_reset_ms (spread {spread:.0f}x)")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="*", help="log files, directories, or '-' for stdin")
    parser.add_argument("--flag-over", type=float, default=500.0,
                        help="phases above this many ms are flagged as outliers (default: 500)")
    parser.add_argument("--json", action="store_true", help="emit the report as JSON")
    parser.add_argument("--only", action="append", default=[],
                        help="keep only phases whose name contains this substring (repeatable)")
    parser.add_argument("--fail-on-outlier", action="store_true",
                        help="exit 1 when any selected phase is over the threshold")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.inputs:
        parser.error("at least one input is required (or use --self-test)")

    records = parse_lifecycle_lines(_iter_input_lines(args.inputs))
    if not records:
        print("no job-lifecycle records found in the given inputs", file=sys.stderr)
        return 2

    report = summarize(records, flag_over=args.flag_over, only=tuple(args.only))
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(render_text(report))
    if args.fail_on_outlier and report["outliers"]:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
