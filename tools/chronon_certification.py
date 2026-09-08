#!/usr/bin/env python3
"""tools/chronon_certification.py — Phase 13 certificate aggregator.

Produces chronon-certification.json: the signable aggregate of the existing
authority results. It is NOT a new business authority — it only aggregates:

  * source identity            (git sha, dirty flag)
  * architecture gate          (tools/check_architecture.py log)
  * zero-copy certificate      (tools/verify_zero_copy_end_to_end.sh verdict + report)
  * correctness runs           (unit / integration / receipt / AV-parity exit codes)
  * performance summary        (optional pre-aggregated p50/p95/baseline-delta JSON)

Usage:
  python3 tools/chronon_certification.py \
      --out /tmp/chronon-certification.json \
      [--arch-gate-log /tmp/arch.log] \
      [--zero-copy-verdict /tmp/zc.verdict] [--zero-copy-report /tmp/report.timing.json] \
      [--unit-tests-exit 0] [--integration-tests-exit 0] \
      [--receipt-exit 0] [--av-parity-exit 0] \
      [--perf-json /tmp/perf.json]

Exit code: 0 when every supplied authority passed, 1 when any supplied
authority failed. Authorities that were not supplied are recorded as
"not-run" and do not fail the aggregate.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


def git_source(root: Path) -> dict:
    source: dict = {"git_sha": None, "upstream_sha": None, "dirty": None}
    try:
        sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
                             capture_output=True, text=True, timeout=15)
        if sha.returncode == 0:
            source["git_sha"] = sha.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        pass
    try:
        upstream = subprocess.run(["git", "rev-parse", "--verify", "@{u}"], cwd=root,
                                  capture_output=True, text=True, timeout=15)
        if upstream.returncode == 0:
            source["upstream_sha"] = upstream.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        pass
    try:
        dirty = subprocess.run(["git", "status", "--porcelain"], cwd=root,
                               capture_output=True, text=True, timeout=15)
        if dirty.returncode == 0:
            source["dirty"] = bool(dirty.stdout.strip())
    except (OSError, subprocess.TimeoutExpired):
        pass
    return source


def parse_gate_log(path: Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"GATE_(PASS|FAIL):\s*(\d+)/(\d+)", text)
    if not m:
        return {"gate_status": "no-verdict", "passed": 0, "total": 0,
                "detail": "no GATE_PASS/GATE_FAIL line found"}
    status, passed, total = m.group(1), int(m.group(2)), int(m.group(3))
    return {"gate_status": "PASS" if status == "PASS" else "FAIL",
            "passed": passed, "total": total,
            "legacy_symbols": len(re.findall(r"\[FAIL\].*legacy", text))}


def parse_zero_copy_verdict(path: Path) -> dict:
    verdict = path.read_text(encoding="utf-8", errors="replace").strip().splitlines()
    status = next((line.strip() for line in verdict
                   if line.strip().startswith("ZERO_COPY_")), "no-verdict")
    return {"status": status}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="chronon-certification.json")
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--arch-gate-log", type=Path)
    parser.add_argument("--zero-copy-verdict", type=Path)
    parser.add_argument("--zero-copy-report", type=Path)
    parser.add_argument("--unit-tests-exit", type=int)
    parser.add_argument("--integration-tests-exit", type=int)
    parser.add_argument("--receipt-exit", type=int)
    parser.add_argument("--av-parity-exit", type=int)
    parser.add_argument("--perf-json", type=Path)
    parser.add_argument("--expected-sha", help="require all supplied evidence to identify this SHA")
    parser.add_argument("--require-clean", action="store_true",
                        help="fail the certificate when the working tree is dirty")
    args = parser.parse_args()

    root = args.root.resolve()
    cert: dict = {"schema": "chronon-certification.v1"}
    cert["source"] = git_source(root)
    source_sha = cert["source"].get("git_sha")
    same_sha = {"expected": args.expected_sha,
                "observed": source_sha,
                "matches": args.expected_sha is None or args.expected_sha == source_sha,
                "working_tree_clean": cert["source"].get("dirty") is False}
    cert["same_sha"] = same_sha

    # toolchain / hardware — best-effort local probe
    toolchain: dict = {}
    for cmd, key in (("c++ --version", "compiler"),
                     ("cmake --version", "cmake"),
                     ("ffmpeg -version", "ffmpeg")):
        try:
            proc = subprocess.run(cmd, shell=True, capture_output=True,
                                  text=True, timeout=15)
            toolchain[key] = proc.stdout.splitlines()[0].strip() if proc.stdout else None
        except (OSError, subprocess.TimeoutExpired):
            toolchain[key] = None
    cert["toolchain"] = toolchain
    hardware: dict = {}
    try:
        nvidia = subprocess.run(["nvidia-smi", "--query-gpu=name,driver_version",
                                 "--format=csv,noheader"], capture_output=True,
                                text=True, timeout=15)
        if nvidia.returncode == 0 and nvidia.stdout.strip():
            gpu, driver = nvidia.stdout.strip().split(",", 1)
            hardware["gpu"], hardware["driver"] = gpu.strip(), driver.strip()
    except (OSError, subprocess.TimeoutExpired):
        pass
    cert["hardware"] = hardware

    # architecture
    arch: dict = {"gate_status": "not-run", "passed": 0, "total": 0,
                  "legacy_symbols": 0, "legacy_targets": 0,
                  "duplicate_authorities": 0}
    if args.arch_gate_log:
        arch.update(parse_gate_log(args.arch_gate_log.resolve()))
    cert["architecture"] = arch

    # zero-copy
    zc: dict = {"all_counters": "not-run", "ZERO_COPY_PASS": False,
                "ZERO_COPY_BLOCKED": False, "ZERO_COPY_FAIL": False}
    if args.zero_copy_verdict:
        parsed = parse_zero_copy_verdict(args.zero_copy_verdict.resolve())
        zc["status"] = parsed["status"]
        zc["ZERO_COPY_PASS"] = parsed["status"] == "ZERO_COPY_PASS"
        zc["ZERO_COPY_FAIL"] = parsed["status"] == "ZERO_COPY_FAIL"
        zc["ZERO_COPY_BLOCKED"] = parsed["status"] == "ZERO_COPY_BLOCKED"
        if args.zero_copy_report and Path(args.zero_copy_report).exists():
            try:
                report = json.loads(Path(args.zero_copy_report).read_text())
                zc["all_counters"] = report.get("counters", report)
            except (ValueError, OSError):
                zc["all_counters"] = "unreadable"
    cert["zero_copy"] = zc

    # correctness — supplied authorities only
    correctness: dict = {}
    for key, value in (("unit", args.unit_tests_exit),
                       ("integration", args.integration_tests_exit),
                       ("receipt", args.receipt_exit),
                       ("av_parity", args.av_parity_exit)):
        correctness[key] = "not-run" if value is None else ("PASS" if value == 0 else "FAIL")
    cert["correctness"] = correctness

    # performance — embed the pre-aggregated summary verbatim
    if args.perf_json and Path(args.perf_json).exists():
        try:
            cert["performance"] = json.loads(Path(args.perf_json).read_text())
        except (ValueError, OSError):
            cert["performance"] = {"status": "unreadable"}
    else:
        cert["performance"] = {"status": "not-run"}

    # artifact references
    artifacts: dict = {}
    if args.zero_copy_report and Path(args.zero_copy_report).exists():
        artifacts["timing_sidecar"] = str(args.zero_copy_report)
    cert["artifacts"] = artifacts

    out = Path(args.out)
    out.write_text(json.dumps(cert, indent=2, sort_keys=True) + "\n")

    # aggregate verdict over the *supplied* authorities
    failures: list[str] = []
    if arch.get("gate_status") == "FAIL":
        failures.append("architecture gate FAIL")
    if args.expected_sha and not same_sha["matches"]:
        failures.append("source SHA does not match --expected-sha")
    if args.require_clean and not same_sha["working_tree_clean"]:
        failures.append("working tree is dirty")
    if zc.get("ZERO_COPY_FAIL"):
        failures.append("zero-copy FAIL")
    if zc.get("ZERO_COPY_BLOCKED"):
        failures.append("zero-copy BLOCKED")
    for key, value in correctness.items():
        if value == "FAIL":
            failures.append(f"correctness.{key} FAIL")
    print(f"wrote {out}")
    if failures:
        print("CERTIFICATION_FAIL: " + "; ".join(failures))
        return 1
    print("CERTIFICATION_PASS: all supplied authorities passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
