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
    ap.add_argument("--out", type=pathlib.Path)
    ns = ap.parse_args()
    files = [p for p in ns.root.rglob("*") if p.is_file()]
    total = sum(p.stat().st_size for p in files)
    metrics = {"root": str(ns.root), "files": len(files), "bytes": total}
    if ns.out:
        ns.out.write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n")
    print(json.dumps(metrics, sort_keys=True))
    if total > ns.max_bytes or len(files) > ns.max_files:
        print(f"BUILD_BUDGET_FAIL: bytes={total}/{ns.max_bytes}, files={len(files)}/{ns.max_files}", file=sys.stderr)
        return 1
    print("BUILD_BUDGET_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
