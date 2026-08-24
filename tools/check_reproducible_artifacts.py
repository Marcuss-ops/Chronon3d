#!/usr/bin/env python3
"""Compare two clean-build artifact trees byte-for-byte."""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import sys
from fnmatch import fnmatch


def manifest(root: pathlib.Path, patterns: list[str]) -> dict[str, str]:
    result = {}
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        relative = str(path.relative_to(root))
        if patterns and not any(fnmatch(relative, pattern) for pattern in patterns):
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        result[relative] = digest
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("left", type=pathlib.Path)
    ap.add_argument("right", type=pathlib.Path)
    ap.add_argument(
        "--pattern",
        action="append",
        default=[],
        help="relative artifact glob to compare; repeat for multiple patterns",
    )
    ns = ap.parse_args()
    left, right = manifest(ns.left, ns.pattern), manifest(ns.right, ns.pattern)
    if not left and not right:
        print("REPRODUCIBLE_BUILD_FAIL: no artifacts matched", file=sys.stderr)
        return 2
    if left != right:
        print("REPRODUCIBLE_BUILD_FAIL: artifact trees differ", file=sys.stderr)
        for name in sorted(set(left) | set(right)):
            if left.get(name) != right.get(name):
                print(f"  {name}: {left.get(name, '<missing>')} != {right.get(name, '<missing>')}", file=sys.stderr)
        return 1
    print(f"REPRODUCIBLE_BUILD_PASS: {len(left)} artifacts identical")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
