#!/usr/bin/env python3
"""Runtime benchmark: output-contract probe vs in-process fast path.

Runs the env-gated doctest benchmark several times and prints the median
probe/fast cost and the per-100-items saving. The benchmark itself creates a
real 1920x1080 H.264 MP4 with ffmpeg and times only the verification call.

Usage:
    python3 tools/bench_output_contract_fastpath.py
    python3 tools/bench_output_contract_fastpath.py --runs 5
"""

from __future__ import annotations

import argparse
import os
import re
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = (
    ROOT / "build/chronon/linux-video-test/tests/chronon3d_media_video_tests"
)
FILTER = "*runtime fast path benchmark*"
BENCH_RE = re.compile(
    r"BENCH frames=(\d+) bytes=(\d+) "
    r"probe_p50_ms=([0-9.]+) fast_p50_ms=([0-9.]+) speedup_x=([0-9.]+) "
    r"probe_min_ms=([0-9.]+) fast_min_ms=([0-9.]+)"
)


def run_once(binary: Path, run: int) -> tuple[float, float, float, float, int, int]:
    env = os.environ.copy()
    env["CHRONON_BENCH_OUTPUT_CONTRACT"] = "1"
    proc = subprocess.run(
        [str(binary), f"-tc={FILTER}"],
        cwd=ROOT,
        env=env,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"benchmark run {run} failed (exit {proc.returncode})")

    match = BENCH_RE.search(proc.stdout)
    if match is None:
        sys.stderr.write(proc.stdout)
        raise SystemExit(f"benchmark run {run}: BENCH line not found")

    frames, size, probe_p50, fast_p50, speedup, probe_min, fast_min = match.groups()
    return (
        float(probe_p50),
        float(fast_p50),
        float(speedup),
        float(probe_min),
        int(frames),
        int(size),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, default=3, help="number of benchmark runs")
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    args = parser.parse_args()

    if not args.binary.is_file():
        raise SystemExit(f"benchmark binary not found: {args.binary}")
    if args.runs < 1:
        raise SystemExit("--runs must be >= 1")

    rows: list[tuple[float, float, float, float, int, int]] = []
    for run in range(1, args.runs + 1):
        probe_p50, fast_p50, speedup, probe_min, frames, size = run_once(
            args.binary, run
        )
        rows.append((probe_p50, fast_p50, speedup, probe_min, frames, size))
        print(
            f"run {run}: probe_p50={probe_p50:.3f} ms  "
            f"fast_p50={fast_p50:.3f} ms  speedup={speedup:.2f}x  "
            f"probe_min={probe_min:.3f} ms"
        )

    probe_med = statistics.median(row[0] for row in rows)
    fast_med = statistics.median(row[1] for row in rows)
    saved = probe_med - fast_med

    print()
    print(f"artifact: {rows[0][4]} frames, {rows[0][5]} bytes")
    print(
        f"median over {len(rows)} runs: "
        f"probe={probe_med:.3f} ms  fast={fast_med:.3f} ms  "
        f"saved={saved:.3f} ms  speedup={probe_med / fast_med:.2f}x"
    )
    print(f"per 100 items: ~{saved * 100 / 1000:.1f} s saved")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
