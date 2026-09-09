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
import tempfile
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


def extract_evidence_shas(path: Path) -> set[str]:
    """Scan an evidence file for explicit git SHA references."""
    if not path.is_file():
        return set()
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return set()
    explicit_patterns = [
        r'tested_sha=([0-9a-fA-F]{40})',
        r'["\'](?:git_sha|sha|head_sha)["\']\s*:\s*["\']([0-9a-fA-F]{40})["\']',
        r'commit\s+([0-9a-fA-F]{40})',
        r'SHA:\s*([0-9a-fA-F]{40})',
        r'HEAD\s+([0-9a-fA-F]{40})',
    ]
    found = set()
    for pattern in explicit_patterns:
        for m in re.finditer(pattern, text):
            found.add(m.group(1).lower())
    return found


def run_selftest() -> int:
    """Automated self-test for certification aggregator failure modes."""
    print("chronon_certification selftest: running invariant checks...")
    with tempfile.TemporaryDirectory() as tmpdir:
        td = Path(tmpdir)
        repo = td / "clean_repo"
        repo.mkdir()
        remote = td / "remote_repo"
        remote.mkdir()
        subprocess.run(["git", "init", "--bare", "-b", "main"], cwd=remote, capture_output=True, check=True)
        subprocess.run(["git", "init", "-b", "main"], cwd=repo, capture_output=True, check=True)
        subprocess.run(["git", "config", "user.email", "test@chronon3d"], cwd=repo, capture_output=True, check=True)
        subprocess.run(["git", "config", "user.name", "Test Runner"], cwd=repo, capture_output=True, check=True)
        subprocess.run(["git", "remote", "add", "origin", str(remote)], cwd=repo, capture_output=True, check=True)
        subprocess.run(["git", "commit", "--allow-empty", "-m", "init"], cwd=repo, capture_output=True, check=True)
        subprocess.run(["git", "push", "-u", "origin", "main"], cwd=repo, capture_output=True, check=True)
        valid_sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True, check=True).stdout.strip()

        mismatched_sha = "1111111111111111111111111111111111111111"

        out_json = td / "cert.json"
        # 1. Missing mandatory evidence check
        res = subprocess.run([
            sys.executable, __file__,
            "--root", str(repo),
            "--strict",
            "--expected-sha", valid_sha,
            "--out", str(out_json),
        ], capture_output=True, text=True)
        assert res.returncode != 0, "Expected failure on missing mandatory evidence"
        assert "missing mandatory evidence" in res.stdout or "missing mandatory evidence" in res.stderr, (
            f"Expected 'missing mandatory evidence' error, got: {res.stdout} {res.stderr}"
        )

        # 2. Mixed SHA evidence check
        bad_arch_log = td / "arch_bad.log"
        bad_arch_log.write_text(f"tested_sha={mismatched_sha}\nGATE_PASS: 148/148 architecture rule(s) PASSED\n")
        zc_verdict = td / "zc.verdict"
        zc_verdict.write_text("ZERO_COPY_PASS\n")

        res = subprocess.run([
            sys.executable, __file__,
            "--root", str(repo),
            "--strict",
            "--expected-sha", valid_sha,
            "--arch-gate-log", str(bad_arch_log),
            "--zero-copy-verdict", str(zc_verdict),
            "--unit-tests-exit", "0",
            "--integration-tests-exit", "0",
            "--out", str(out_json),
        ], capture_output=True, text=True)
        assert res.returncode != 0, "Expected failure on mixed-SHA evidence"
        assert "mixed-SHA evidence" in res.stdout or "mixed-SHA evidence" in res.stderr, (
            f"Expected 'mixed-SHA evidence' error, got: {res.stdout} {res.stderr}"
        )

        # 3. Expected SHA mismatch check
        res = subprocess.run([
            sys.executable, __file__,
            "--root", str(repo),
            "--strict",
            "--expected-sha", mismatched_sha,
            "--out", str(out_json),
        ], capture_output=True, text=True)
        assert res.returncode != 0, "Expected failure on expected-sha mismatch"
        assert "does not match --expected-sha" in res.stdout or "does not match --expected-sha" in res.stderr, (
            f"Expected SHA mismatch error, got: {res.stdout} {res.stderr}"
        )

        # 4. Valid passing run
        good_arch_log = td / "arch_good.log"
        good_arch_log.write_text(f"tested_sha={valid_sha}\nGATE_PASS: 148/148 architecture rule(s) PASSED\n")
        res = subprocess.run([
            sys.executable, __file__,
            "--root", str(repo),
            "--strict",
            "--expected-sha", valid_sha,
            "--arch-gate-log", str(good_arch_log),
            "--zero-copy-verdict", str(zc_verdict),
            "--unit-tests-exit", "0",
            "--integration-tests-exit", "0",
            "--out", str(out_json),
        ], capture_output=True, text=True)
        assert res.returncode == 0, f"Expected passing certification run, got exit {res.returncode}: {res.stdout} {res.stderr}"
        assert out_json.is_file(), "Expected output json file to be created"

    print("chronon_certification selftest: all assertions passed.")
    return 0


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
    parser.add_argument("--strict", action="store_true",
                        help="enforce strict certification: require --expected-sha, reject dirty tree, HEAD != origin/main, mixed-SHA evidence, and missing mandatory evidence")
    parser.add_argument("--require-clean", action="store_true",
                        help="fail the certificate when the working tree is dirty")
    parser.add_argument("--selftest", action="store_true", help="run self-test suite and exit")
    args = parser.parse_args()

    if args.selftest:
        return run_selftest()

    root = args.root.resolve()
    cert: dict = {"schema": "chronon-certification.v1", "strict": args.strict}
    cert["source"] = git_source(root)
    source_sha = cert["source"].get("git_sha")
    upstream_sha = cert["source"].get("upstream_sha")

    expected_sha = args.expected_sha.strip().lower() if args.expected_sha else None
    observed_sha = source_sha.strip().lower() if source_sha else None
    upstream_sha_clean = upstream_sha.strip().lower() if upstream_sha else None

    same_sha = {
        "expected": expected_sha,
        "observed": observed_sha,
        "upstream": upstream_sha_clean,
        "matches": expected_sha is None or (observed_sha is not None and expected_sha == observed_sha),
        "in_sync_with_upstream": (observed_sha is not None and upstream_sha_clean is not None and observed_sha == upstream_sha_clean),
        "working_tree_clean": cert["source"].get("dirty") is False,
    }
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

    # aggregate verdict and strict validation checks
    failures: list[str] = []

    # 1. Expected SHA check
    if expected_sha and not same_sha["matches"]:
        failures.append(f"source SHA ({observed_sha}) does not match --expected-sha ({expected_sha})")

    # 2. Strict mode checks
    if args.strict:
        if not expected_sha:
            failures.append("--strict mode requires an explicit --expected-sha")
        if not same_sha["working_tree_clean"]:
            failures.append("working tree is dirty (uncommitted changes present)")
        if not same_sha["in_sync_with_upstream"]:
            failures.append(f"local HEAD ({observed_sha}) != origin/main ({upstream_sha_clean})")

        # Mixed-SHA evidence scan
        evidence_files = [
            ("arch_gate_log", args.arch_gate_log),
            ("zero_copy_verdict", args.zero_copy_verdict),
            ("zero_copy_report", args.zero_copy_report),
            ("perf_json", args.perf_json),
        ]
        target_sha = expected_sha or observed_sha
        for label, file_path in evidence_files:
            if file_path and file_path.is_file():
                found_shas = extract_evidence_shas(file_path.resolve())
                for fsha in found_shas:
                    if target_sha and fsha != target_sha:
                        failures.append(
                            f"mixed-SHA evidence in {label} ({file_path.name}): "
                            f"cites SHA {fsha} != expected {target_sha}"
                        )

        # Missing mandatory evidence
        missing_mandatory = []
        if not args.arch_gate_log:
            missing_mandatory.append("arch-gate-log")
        if not args.zero_copy_verdict:
            missing_mandatory.append("zero-copy-verdict")
        if args.unit_tests_exit is None:
            missing_mandatory.append("unit-tests-exit")
        if args.integration_tests_exit is None:
            missing_mandatory.append("integration-tests-exit")
        if missing_mandatory:
            failures.append(f"missing mandatory evidence: {', '.join(missing_mandatory)}")

    elif args.require_clean and not same_sha["working_tree_clean"]:
        failures.append("working tree is dirty")

    # 3. Authority verdicts
    if arch.get("gate_status") == "FAIL":
        failures.append("architecture gate FAIL")
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
    print("CERTIFICATION_PASS: all required authorities verified and passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
