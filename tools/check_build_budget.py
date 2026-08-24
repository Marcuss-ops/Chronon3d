#!/usr/bin/env python3
"""Enforce size and dependency-count budgets for a build artifact directory."""
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("root", type=pathlib.Path)
    ap.add_argument("--max-bytes", type=int, required=True)
    ap.add_argument("--max-files", type=int, default=100000)
    ap.add_argument("--binary", action="append", type=pathlib.Path,
                    help="ELF binary to inspect with ldd (repeatable)")
    ap.add_argument("--max-dependencies", type=int, default=None)
    ap.add_argument("--out", type=pathlib.Path)
    ns = ap.parse_args()
    files = [p for p in ns.root.rglob("*") if p.is_file()]
    total = sum(p.stat().st_size for p in files)
    dependency_names = set()
    for binary in ns.binary or []:
        try:
            output = subprocess.check_output(["ldd", str(binary)], text=True,
                                             stderr=subprocess.STDOUT)
        except (OSError, subprocess.CalledProcessError) as exc:
            print(f"BUILD_BUDGET_FAIL: cannot inspect dependencies for {binary}: {exc}",
                  file=sys.stderr)
            return 1
        for line in output.splitlines():
            fields = line.strip().split()
            if fields and ("=>" in fields or fields[0].startswith("/")):
                dependency_names.add(fields[0] if fields[0].startswith("/") else fields[2])
    metrics = {"root": str(ns.root), "files": len(files), "bytes": total,
               "dependencies": len(dependency_names)}
    if ns.out:
        ns.out.write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n")
    print(json.dumps(metrics, sort_keys=True))
    if (total > ns.max_bytes or len(files) > ns.max_files or
            (ns.max_dependencies is not None and len(dependency_names) > ns.max_dependencies)):
        print(f"BUILD_BUDGET_FAIL: bytes={total}/{ns.max_bytes}, files={len(files)}/{ns.max_files}, "
              f"dependencies={len(dependency_names)}/{ns.max_dependencies if ns.max_dependencies is not None else 'unlimited'}",
              file=sys.stderr)
        return 1
    print("BUILD_BUDGET_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
