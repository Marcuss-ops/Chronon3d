#!/usr/bin/env python3
""":"
exec python3 "$0" "$@"
"""
# ==============================================================================
# tools/check_public_headers.py — Gate driven by Chronon3DPublicHeaders.cmake
#
# Invocation (3 valid forms, post-fix):
#   1. python3 tools/check_public_headers.py --compile-commands <path> --manifest <path> --out <path>  (canonical)
#   2. tools/check_public_headers.py --compile-commands <path> --manifest <path> --out <path>  (direct, chmod +x applied)
#   3. bash tools/check_public_headers.py ...  (polyglot: auto-routes to python3 via the """:" + exec magic)
#
# All 3 forms invoke the Python code below. The polyglot header (lines 1-4)
# makes the script bash-callable (the prior rot: `bash <script>` failed with
# Python-syntax parse errors). Per the 13th-pass code-reviewer's 3-step
# diagnostic, the script body is valid Python; the rot was strictly the
# invocation pattern + lack of execute permission.
# ==============================================================================
#
# Single source of truth for the V0.1 SDK public-header surface is the
# CMake manifest cmake/Chronon3DPublicHeaders.cmake (parsed below —
# lightweight regex; no `cmake -P` invocation required).  This script
# derives THREE gates from the manifest:
#
#   Gate A  no /test/ in public paths        (fail-loud)
#     After relocating test-only inspectors, the public `include/`
#     tree must contain no `test/` directory.  Any hit means a
#     test-only or diagnostic-only header slipped back into the
#     OPP-public surface and would be installed by the SDK.
#
#   Gate B  no public header transitively pulls a non-manifest header
#                                            (WARN, never fail)
#     The V0.1 SDK umbrella header (#include <chronon3d/chronon3d.hpp>)
#     and transitional internal paths may still pull transitives that
#     are not listed in the manifest at this stage.  We surface them as
#     `WARN: ...` lines so CI keeps working while the OPP-side
#     pruning backlog is consumed; the strict slice (Gate B-strict,
#     tracking via docs/CURRENT_STATUS.md) is targeted for V0.2.
#
#   Gate C  each public header compiles standalone
#                                            (fail-loud)
#     The standard `clang -std=c++20 -fsyntax-only` check; flags are
#     derived from the first compile_commands.json entry (chronon3d
#     heaviest-pulling TU) so all transitive third-party includes
#     (spdlog, fmt, glm, xxhash, etc.) are satisfied.
#
# Usage:
#   python3 tools/check_public_headers.py \
#     --compile-commands build/chronon/linux-ci-lean-gate/compile_commands.json \
#     --manifest cmake/Chronon3DPublicHeaders.cmake \
#     --out out
# ==============================================================================

import argparse
import json
import os
import re
import subprocess
import sys


# Path normalisation: the manifest emits paths as
#   "${CMAKE_SOURCE_DIR}/include/chronon3d/<sub>/<file>.hpp"
# Convert back to a relative form suitable for `os.walk` and `open`
# inside the source tree.
_CMAKE_SRC_LIT = "${CMAKE_SOURCE_DIR}/"


def _parse_manifest(manifest_path: str) -> list[str]:
    """Parse cmake/Chronon3DPublicHeaders.cmake and return the manifest
    list as POSIX paths RELATIVE to the repo root.

    Robust text-level parse of the `set(CHRONON3D_PUBLIC_HEADERS …)`
    block — no `cmake -P` invocation required (the script runs without
    a configured build tree).
    """
    with open(manifest_path) as f:
        src = f.read()
    m = re.search(r"set\s*\(\s*CHRONON3D_PUBLIC_HEADERS(.*?)\)", src, re.DOTALL)
    if not m:
        raise SystemExit(
            f"FAIL: could not find `set(CHRONON3D_PUBLIC_HEADERS ...)` in "
            f"{manifest_path} — manifest schema changed?"
        )

    body = m.group(1)
    out = []
    for line in body.splitlines():
        line = line.strip()
        # Drop comments / blank lines.
        if not line or line.startswith("#"):
            continue
        # Strip surrounding quotes and trailing comma.
        line = line.strip().strip('"').strip("'").rstrip(",").strip()
        if not line:
            continue
        # Normalise ${CMAKE_SOURCE_DIR}/… → relative repo-root path.
        if line.startswith(_CMAKE_SRC_LIT):
            line = line[len(_CMAKE_SRC_LIT):]
        out.append(line)
    return out


def _gate_no_test_paths(public_root: str = "include/chronon3d") -> int:
    """Gate A — fail-loud if any /test/ directory is present under the
    OPP-public tree.  Returns 0 on PASS, 1 on FAIL.
    """
    hits = []
    if os.path.isdir(public_root):
        for entry in os.listdir(public_root):
            sub = os.path.join(public_root, entry)
            # Direct child named 'test' (path contains '/test/' trivially):
            if os.path.isdir(sub) and entry == "test":
                hits.append(sub + "/")
            # Otherwise recurse looking for any 'test' segment.
            if os.path.isdir(sub):
                for root, dirs, _ in os.walk(sub):
                    if "test" in dirs:
                        hits.append(os.path.join(root, "test") + "/")
    if hits:
        for h in sorted(set(hits)):
            print(f"  FAIL (Gate A): public /test/ path present: {h}", file=sys.stderr)
        return 1
    return 0


_INCL_RE = re.compile(r'#\s*include\s*[<"]([^>"]+)[>"]')


def _gate_off_manifest_includes(manifest_paths: list[str]) -> int:
    """Gate B — emit `WARN: ...` lines for any OPP-public (`chronon3d/...`)
    `#include` that is not part of the manifest.  Third-party includes
    (`<spdlog/spdlog.h>`, `<fmt/core.h>`, etc.) are SKIPPED entirely:
    Gate B governs only the OPP-public surface; third-party transitives
    are policy-free here.  ALWAYS returns 0 (WARN-only by design — see
    file-level docstring; the strict slice is targeted for V0.2).
    """
    manifest_set = set(manifest_paths)
    warn_count = 0
    for hdr in manifest_paths:
        if not os.path.isfile(hdr):
            print(f"  WARN (Gate B): manifest path missing on disk: {hdr}", file=sys.stderr)
            continue
        try:
            with open(hdr) as f:
                src = f.read()
        except OSError as ex:
            print(f"  WARN (Gate B): cannot read {hdr}: {ex}", file=sys.stderr)
            continue
        for inc in _INCL_RE.findall(src):
            # Gate B governs OPP-public only.  Third-party headers (anything
            # not starting with `chronon3d/`) are out of scope.
            if not inc.startswith("chronon3d/"):
                continue
            # All OPP-public includes are normalised to the manifest path form
            # (`include/chronon3d/<sub>/<file>.hpp`) for membership lookup.
            rel = "include/" + inc
            if rel in manifest_set:
                continue
            warn_count += 1
            print(
                f"  WARN (Gate B): {hdr} includes '{inc}' which is NOT in the manifest",
                file=sys.stderr,
            )
    return 0


def _gate_standalone_compile(
    manifest_paths: list[str],
    compile_commands_db: list[dict],
    out_dir: str,
) -> int:
    """Gate C — fail-loud if any public header does not compile
    standalone.  Flags are derived from the first compile_commands.json
    entry (the heaviest-pulling OPP TU, normally the one that pulls
    spdlog + fmt + glm + xxhash transitively).
    """
    failures = []
    if not compile_commands_db:
        print("  WARN (Gate C): compile_commands.json is empty; skipping standalone check",
              file=sys.stderr)
        return 0
    entry = compile_commands_db[0]
    cwd = entry.get("directory", ".")
    cmd = list(entry.get("command") or [])
    if cmd and (cmd[-1].endswith(".cpp") or cmd[-1].endswith(".cc")):
        cmd.pop()
    # Make the flag template deterministic.
    cmd = [c for c in cmd if c not in ("-c", "-o")]

    for i, hdr in enumerate(manifest_paths, start=1):
        # Translate repo-relative path to the include form (the manifest
        # stores full paths; we need the angle-bracket form).
        angle = hdr
        if angle.startswith("include/"):
            angle = angle[len("include/"):]
        tu = os.path.join(out_dir, f"check_{i}.cpp")
        with open(tu, "w") as f:
            f.write(f"#include <{angle}>\nint main() {{}}\n")
        log_path = os.path.join(out_dir, f"check_{i}.log")
        new_cmd = cmd + ["-fsyntax-only", tu]
        # Capture original stderr/stdout to identify env-only failures
        # (missing third-party deps) vs true header regressions.
        try:
            res = subprocess.run(
                new_cmd, cwd=cwd, capture_output=True, text=True, timeout=120
            )
        except subprocess.TimeoutExpired:
            res = subprocess.CompletedProcess(
                new_cmd, returncode=1, stdout="", stderr="TIMEOUT",
            )
        with open(log_path, "w") as lf:
            lf.write(res.stdout + res.stderr)
        if res.returncode != 0:
            failures.append(hdr)
            print(f"  FAIL (Gate C): {hdr}", file=sys.stderr)
            # Print the first 15 log lines so the failure is self-contained.
            with open(log_path) as lf:
                head = "".join(lf.readlines()[:15])
                print(head, end="", file=sys.stderr)
    if failures:
        with open(os.path.join(out_dir, "failures.txt"), "w") as fp:
            fp.write("\n".join(failures) + "\n")
        print(
            f"\n  FAIL (Gate C): {len(failures)}/{len(manifest_paths)} public header(s) "
            f"failed standalone compilation.  See {out_dir}/failures.txt.",
            file=sys.stderr,
        )
        return 1
    print(f"  PASS (Gate C): all {len(manifest_paths)} public headers compile standalone.")
    return 0


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--compile-commands", required=True,
                   help="Path to compile_commands.json from a prior cmake configure")
    p.add_argument("--manifest", required=True,
                   help="Path to the V0.1 SDK public-header manifest "
                          "(cmake/Chronon3DPublicHeaders.cmake).  This is the "
                          f"SINGLE SOURCE OF TRUTH for the public surface.")
    p.add_argument("--out", required=True,
                   help="Output directory (TUs, logs, failures.txt)")
    args = p.parse_args()

    os.makedirs(args.out, exist_ok=True)
    # Truncate the failure log so older runs don't leave stale entries.
    open(os.path.join(args.out, "failures.txt"), "w").close()

    # ── Parse manifest ─────────────────────────────────────────────────
    try:
        manifest = _parse_manifest(args.manifest)
    except SystemExit as ex:
        print(ex, file=sys.stderr)
        return 2
    if not manifest:
        print(f"FAIL: manifest {args.manifest} parsed to empty list",
              file=sys.stderr)
        return 2
    print(f"Manifest derived {len(manifest)} public header(s) from {args.manifest}.")

    # ── Gate A — no /test/ in public paths ─────────────────────────────
    rc = 0
    if _gate_no_test_paths() != 0:
        rc = 1
        print("  => Gate A FAIL", file=sys.stderr)

    # ── Gate B — WARN on off-manifest includes (never fail) ───────────
    _gate_off_manifest_includes(manifest)

    # ── Gate C — standalone compile (fail-loud) ────────────────────────
    try:
        with open(args.compile_commands) as f:
            db = json.load(f)
    except (OSError, json.JSONDecodeError) as ex:
        print(f"FAIL: cannot read compile_commands.json: {ex}", file=sys.stderr)
        return 2
    if _gate_standalone_compile(manifest, db, args.out) != 0:
        rc = 1

    if rc == 0:
        print("All gates PASSED.")
    else:
        print("One or more gates FAILED — see stderr above.", file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main())

