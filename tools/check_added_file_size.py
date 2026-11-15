#!/usr/bin/env python3
"""Reject newly added repository files larger than the source-control budget."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True,
                        help="git diff base expression (for example origin/main...HEAD)")
    parser.add_argument("--max-mib", type=int, default=1)
    args = parser.parse_args()

    changed = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=A", "-z", args.base],
        check=True, capture_output=True,
    ).stdout.split(b"\0")
    limit = args.max_mib * 1024 * 1024
    oversized: list[tuple[str, int]] = []
    for raw_path in changed:
        if not raw_path:
            continue
        path = Path(raw_path.decode("utf-8", errors="surrogateescape"))
        try:
            size = path.stat().st_size
        except OSError as error:
            print(f"cannot stat added file {path}: {error}", file=sys.stderr)
            return 2
        if size > limit:
            oversized.append((path.as_posix(), size))

    if oversized:
        for path, size in oversized:
            print(f"{path}: {size} bytes exceeds {args.max_mib} MiB")
        print("Store large evidence/media outside the git tree and retain a manifest plus SHA-256.")
        return 1
    print(f"added-file size gate PASS (limit {args.max_mib} MiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
