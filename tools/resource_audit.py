#!/usr/bin/env python3
"""Collect and compare Linux process resource snapshots for leak certification."""
from __future__ import annotations

import argparse
import json
import pathlib
import sys


def snapshot(pid: int) -> dict[str, int]:
    proc = pathlib.Path("/proc") / str(pid)
    if not proc.is_dir():
        raise RuntimeError(f"process does not exist: {pid}")
    fd = len(list((proc / "fd").iterdir()))
    maps = sum(1 for _ in (proc / "maps").open())
    threads = len(list((proc / "task").iterdir()))
    status = {}
    for line in (proc / "status").read_text().splitlines():
        if line.startswith(("VmRSS:", "VmPeak:")):
            status[line.split(":", 1)[0]] = int(line.split()[1])
    return {"fd": fd, "mmap_regions": maps, "threads": threads, **status}


def main() -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="command", required=True)
    take = sub.add_parser("snapshot")
    take.add_argument("pid", type=int)
    take.add_argument("--out", required=True, type=pathlib.Path)
    compare = sub.add_parser("compare")
    compare.add_argument("before", type=pathlib.Path)
    compare.add_argument("after", type=pathlib.Path)
    compare.add_argument("--allow", action="append", default=[], help="allowed positive delta KEY=N")
    ns = ap.parse_args()
    if ns.command == "snapshot":
        data = snapshot(ns.pid)
        ns.out.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
        print(json.dumps(data, sort_keys=True))
        return 0
    before, after = json.loads(ns.before.read_text()), json.loads(ns.after.read_text())
    allowed = {}
    for item in ns.allow:
        key, value = item.split("=", 1)
        allowed[key] = int(value)
    keys = sorted(set(before) | set(after))
    failures = []
    for key in keys:
        delta = int(after.get(key, 0)) - int(before.get(key, 0))
        if delta > allowed.get(key, 0):
            failures.append(f"{key}: {before.get(key, 0)} -> {after.get(key, 0)} (delta={delta}, allowed={allowed.get(key, 0)})")
    for key in keys:
        print(f"RESOURCE_AUDIT: {key} before={before.get(key, 0)} after={after.get(key, 0)} delta={int(after.get(key, 0))-int(before.get(key, 0))}")
    if failures:
        print("RESOURCE_LEAK_FAIL:", *failures, sep="\n  ", file=sys.stderr)
        return 1
    print("RESOURCE_LEAK_PASS: no resource count exceeded its allowance")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
