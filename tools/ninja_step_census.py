#!/usr/bin/env python3
"""ninja_step_census.py — count Ninja build steps per CMake target.

Exports the configured Ninja DAG for one or more aggregate targets and
classifies every edge, so "why does this build take N steps" has a
per-target answer instead of a guess.

  edges     all commands in the target's subgraph (`ninja -t commands`)
  pending   edges Ninja would run right now (`ninja -n`); 0 on a clean tree,
            anything else after a completed build signals stale deps
            (see docs/CCACHE-AUDIT-2026-09-10.md §ninja_deps)
  compile   commands containing ` -c ` (unity batches count once)
  link      compiler/archiver driver producing a binary or shared library
  archive   `ar qc` / `ranlib` static archives
  other     cmake -E helpers, schema generation, etc.

Usage:
  python3 tools/ninja_step_census.py [--build-dir DIR] [--json out.json]
                                     [TARGET ...]

Defaults: DIR=build/chronon/linux-fast-dev,
targets=chronon3d_cli chronon3d_tests_fast chronon3d_dev_fast chronon3d_tests.
Exit code 0 on success, 2 when the build directory is not configured.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_TARGETS = (
    "chronon3d_cli",
    "chronon3d_tests_fast",
    "chronon3d_dev_fast",
    "chronon3d_tests",
)


def ninja(build_dir: Path, args: list[str]) -> str:
    return subprocess.run(["ninja", "-C", str(build_dir), *args],
                          capture_output=True, text=True).stdout


def classify(target: str, build_dir: Path) -> dict:
    edges_out = ninja(build_dir, ["-t", "commands", target])
    lines = [l for l in edges_out.splitlines() if l.strip()]
    pending_out = ninja(build_dir, ["-n", target])
    pending = len([l for l in pending_out.splitlines() if l.strip() and "no work to do" not in l])

    result = {
        "target": target,
        "edges": len(lines),
        "pending": pending,
        "compile": 0,
        "link": 0,
        "archive": 0,
        "other": 0,
        "per_target": {},
    }
    for line in lines:
        if " -c " in line:
            result["compile"] += 1
            m = re.search(r"-o (\S+)", line)
            obj = m.group(1) if m else "?"
            tm = re.search(r"CMakeFiles/([^/]+)\.dir/", obj)
            key = tm.group(1) if tm else obj
            bucket = result["per_target"].setdefault(key, {"edges": 0, "sources": []})
            bucket["edges"] += 1
            src = line.split(" -c ", 1)[1].strip().split()[0]
            bucket["sources"].append(src)
        elif "/usr/bin/ar qc" in line or "ranlib" in line:
            result["archive"] += 1
        elif re.search(r" (-o) (\S+)", line) and re.search(r"(c\+\+|/usr/bin/cc|clang|mold|ld) ", line):
            result["link"] += 1
        else:
            result["other"] += 1
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default="build/chronon/linux-fast-dev")
    ap.add_argument("--json", default="")
    ap.add_argument("--no-loc", action="store_true")
    ap.add_argument("targets", nargs="*", default=list(DEFAULT_TARGETS))
    args = ap.parse_args()

    build_dir = Path(args.build_dir).resolve()
    if not (build_dir / "build.ninja").is_file():
        print(f"ninja_step_census: {build_dir}/build.ninja not found "
              f"(configure the preset first)", file=sys.stderr)
        return 2

    reports = {}
    print(f"{'target':34s} {'edges':>6s} {'pending':>8s} {'compile':>8s} "
          f"{'link':>5s} {'arch':>5s} {'other':>6s}")
    for target in args.targets:
        r = classify(target, build_dir)
        reports[target] = r
        print(f"{target:34s} {r['edges']:6d} {r['pending']:8d} {r['compile']:8d} "
              f"{r['link']:5d} {r['archive']:5d} {r['other']:6d}")

    if args.json:
        if not args.no_loc and "chronon3d_dev_fast" in reports:
            for bucket in reports["chronon3d_dev_fast"]["per_target"].values():
                loc = 0
                for src in bucket["sources"]:
                    try:
                        with open(src, "rb") as f:
                            loc += sum(1 for _ in f)
                    except OSError:
                        pass
                bucket["loc"] = loc
        Path(args.json).write_text(json.dumps(reports, indent=1), encoding="utf-8")
        print(f"json written: {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
