#!/usr/bin/env python3
# ==============================================================================
# tools/check_public_headers.py — Gate 3 helper for .github/workflows/gates.yml
# ==============================================================================
#
# Public-header check: every header under include/chronon3d/ must compile
# standalone (with `int main(){}`) without source-side errors. Surfaces:
#   - stale transitive includes (a recent refactor tightened prerequisites)
#   - header typo regressions (TICKET-003's `<chrono3d/...>` class of bug)
#   - namespace/path misalignment
#
# Strategy: read compile_commands.json from the build dir, take the flags
# of one chronon3d_pipeline TU as a template (it pulls in the heaviest set
# of transitive third-party includes: spdlog, fmt, glm, etc.), and apply
# those flags + `-fsyntax-only` to each header.
#
# Usage:
#   python3 tools/check_public_headers.py \
#     --compile-commands build/chronon/linux-lean-dev/compile_commands.json \
#     --headers-list headers.txt \
#     --out out

import argparse
import json
import os
import subprocess
import sys


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--compile-commands", required=True,
                   help="Path to compile_commands.json from a prior cmake configure")
    p.add_argument("--headers-list", required=True,
                   help="Path to a newline-separated list of header paths to check")
    p.add_argument("--out", required=True,
                   help="Directory where TUs, log files, and failures.txt are written")
    p.add_argument("--template-entry", type=int, default=0,
                   help="Index into compile_commands.json to use as flag template (default 0)")
    args = p.parse_args()

    os.makedirs(args.out, exist_ok=True)
    failures_path = os.path.join(args.out, "failures.txt")
    with open(failures_path, "w") as fp:
        fp.write("")

    with open(args.compile_commands) as f:
        db = json.load(f)
    if not db:
        print("compile_commands.json is empty", file=sys.stderr)
        return 2
    if args.template_entry >= len(db):
        print(f"template-entry {args.template_entry} out of range (db has {len(db)})",
              file=sys.stderr)
        return 2

    entry = db[args.template_entry]
    cwd = entry["directory"]
    cmd = list(entry["command"])
    # Strip the source-file arg (last token) — we substitute our own.
    if cmd and (cmd[-1].endswith(".cpp") or cmd[-1].endswith(".cc")):
        cmd.pop()

    with open(args.headers_list) as fp:
        headers = [h.strip() for h in fp if h.strip()]

    failures = []
    for i, hdr in enumerate(headers, start=1):
        tu = os.path.join(args.out, f"check_{i}.cpp")
        with open(tu, "w") as fp:
            fp.write(f"#include <{hdr}>\nint main() {{}}\n")
        new_cmd = cmd + ["-fsyntax-only", "-c", tu]
        log_path = os.path.join(args.out, f"check_{i}.log")
        try:
            res = subprocess.run(new_cmd, cwd=cwd, capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            res = subprocess.CompletedProcess(new_cmd, returncode=1, stdout="", stderr="TIMEOUT")
        with open(log_path, "w") as lf:
            lf.write(res.stdout + res.stderr)
        if res.returncode != 0:
            failures.append(hdr)
            print(f"  FAIL: {hdr}")
            with open(log_path) as lf:
                head = "".join(lf.readlines()[:15])
                print(head)

    if failures:
        with open(failures_path, "w") as fp:
            fp.write("\n".join(failures) + "\n")
        print(f"\n{len(failures)} header(s) failed standalone compilation.",
              file=sys.stderr)
        return 1

    print(f"All {len(headers)} public headers compile standalone.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
