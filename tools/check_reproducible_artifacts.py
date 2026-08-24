#!/usr/bin/env python3
"""Compare two clean-build artifact trees byte-for-byte."""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import sys


def manifest(root: pathlib.Path) -> dict[str, str]:
    result = {}
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        result[str(path.relative_to(root))] = digest
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("left", type=pathlib.Path)
    ap.add_argument("right", type=pathlib.Path)
    ns = ap.parse_args()
    left, right = manifest(ns.left), manifest(ns.right)
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
