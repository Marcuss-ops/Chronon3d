#!/usr/bin/env python3
"""Verify the recent Chronon3D performance work with generated checks.

Runs:
  - the runtime output-contract fast-path benchmark (3 runs)
  - the media/video suite (mux, receipt, contract)
  - the CLI contract suite
  - the render-graph topology/DoF tests, when built
  - the deterministic suite, when built

Usage:
    python3 tools/verify_recent_perf.py
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build/chronon/linux-video-test/tests"
BENCH = ROOT / "tools/bench_output_contract_fastpath.py"


def run(name: str, args: list[str], timeout: int = 300) -> bool:
    print(f"\n=== {name} ===")
    try:
        proc = subprocess.run(
            args,
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        print(f"[FAIL] {name} (timeout after {timeout}s)")
        return False

    if proc.stdout:
        print(proc.stdout.rstrip())
    if proc.returncode != 0:
        if proc.stderr:
            print(proc.stderr.rstrip(), file=sys.stderr)
        print(f"[FAIL] {name} (exit {proc.returncode})")
        return False
    print(f"[PASS] {name}")
    return True


def main() -> int:
    cli = ROOT / "build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli"
    if cli.is_file():
        os.environ.setdefault("CHRONON3D_CLI_BINARY", str(cli))

    results: dict[str, bool] = {}

    results["output-contract fast path benchmark"] = run(
        "output-contract fast path benchmark",
        [sys.executable, str(BENCH), "--runs", "3"],
    )
    results["media/video suite"] = run(
        "media/video suite",
        [str(BIN / "chronon3d_media_video_tests")],
        timeout=600,
    )
    results["CLI contract suite"] = run(
        "CLI contract suite",
        [str(BIN / "chronon3d_cli_tests"), "-tc=*contract*"],
    )

    render_graph = BIN / "chronon3d_render_graph_tests"
    if render_graph.is_file():
        results["topology animated paths"] = run(
            "topology animated paths",
            [str(render_graph), "-tc=*animated path updates an overlay*"],
        )
        results["DoF graph tests"] = run(
            "DoF graph tests",
            [str(render_graph), "-tc=PR2-RG-DoF*"],
        )
    else:
        print("\n[NOT BUILT] render-graph topology/DoF tests")

    deterministic = BIN / "chronon3d_deterministic_tests"
    if deterministic.is_file():
        results["deterministic suite"] = run(
            "deterministic suite",
            [str(deterministic)],
            timeout=600,
        )
    else:
        print("\n[NOT BUILT] deterministic suite")

    print("\n=== SUMMARY ===")
    for name, ok in results.items():
        print(f"{'PASS' if ok else 'FAIL'}  {name}")
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
