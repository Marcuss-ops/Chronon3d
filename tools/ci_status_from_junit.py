#!/usr/bin/env python3
"""
ci_status_from_junit.py — Generate machine-readable CI status JSON from
ctest JUnit XML output.

Usage:
    python3 tools/ci_status_from_junit.py \
        --junit build/test_results.xml \
        --commit $(git rev-parse HEAD) \
        --workflow "Chronon CI" \
        --out ci_status.json

Output JSON schema:
{
    "schema_version": 1,
    "commit": "abc123...",
    "tested_at": "2026-07-10T14:30:00Z",
    "workflow": "Chronon CI",
    "tests_total": 150,
    "passed": 148,
    "failed": 1,
    "skipped": 1,
    "errors": 0,
    "gates_passed": true,
    "test_suites": [
        {
            "name": "chronon3d_core_tests",
            "tests": 80,
            "passed": 80,
            "failed": 0,
            "skipped": 0,
            "time_seconds": 12.5
        }
    ]
}

Exit codes:
    0 — success, JSON written
    1 — error (missing input, parse failure)
"""

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path


def parse_junit_xml(xml_path: str) -> dict:
    """Parse JUnit XML produced by ctest --output-junit and extract summary."""
    tree = ET.parse(xml_path)
    root = tree.getroot()

    # ctest --output-junit produces a <testsuites> root with <testsuite> children,
    # or a single <testsuite> root. Handle both.
    suites = []
    total_tests = 0
    total_passed = 0
    total_failed = 0
    total_skipped = 0
    total_errors = 0
    total_time = 0.0

    suite_elements = []
    if root.tag == "testsuites":
        suite_elements = list(root)
    elif root.tag == "testsuite":
        suite_elements = [root]
    else:
        print(f"Warning: unexpected root element <{root.tag}>, trying to find testsuites/testsuite", file=sys.stderr)
        suite_elements = root.findall(".//testsuite") or [root]

    for suite in suite_elements:
        if suite.tag != "testsuite":
            continue

        name = suite.get("name", "unknown")
        tests = int(suite.get("tests", "0"))
        failures = int(suite.get("failures", "0"))
        errors_count = int(suite.get("errors", "0"))
        skipped_attr = suite.get("skipped", "0")
        time_s = float(suite.get("time", "0"))

        # Count skipped from individual test cases if suite-level skipped is 0
        skipped = int(skipped_attr) if skipped_attr else 0
        if skipped == 0:
            for tc in suite.findall("testcase"):
                if tc.find("skipped") is not None:
                    skipped += 1

        passed = tests - failures - errors_count - skipped

        suites.append({
            "name": name,
            "tests": tests,
            "passed": max(0, passed),
            "failed": failures,
            "skipped": skipped,
            "errors": errors_count,
            "time_seconds": round(time_s, 2),
        })

        total_tests += tests
        total_passed += max(0, passed)
        total_failed += failures
        total_skipped += skipped
        total_errors += errors_count
        total_time += time_s

    return {
        "tests_total": total_tests,
        "passed": total_passed,
        "failed": total_failed,
        "skipped": total_skipped,
        "errors": total_errors,
        "total_time_seconds": round(total_time, 2),
        "gates_passed": (total_failed == 0 and total_errors == 0),
        "test_suites": suites,
    }


def build_status(junit_data: dict, commit: str, workflow: str) -> dict:
    """Build the full CI status JSON object."""
    return {
        "schema_version": 1,
        "commit": commit,
        "tested_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "workflow": workflow,
        **junit_data,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Generate machine-readable CI status JSON from ctest JUnit XML"
    )
    parser.add_argument(
        "--junit", required=True,
        help="Path to ctest JUnit XML file (--output-junit)"
    )
    parser.add_argument(
        "--commit", required=True,
        help="Git commit SHA being tested"
    )
    parser.add_argument(
        "--workflow", default="Chronon CI",
        help="Workflow name (default: Chronon CI)"
    )
    parser.add_argument(
        "--out", required=True,
        help="Output JSON file path"
    )
    args = parser.parse_args()

    junit_path = Path(args.junit)
    if not junit_path.exists():
        print(f"Error: JUnit XML not found: {junit_path}", file=sys.stderr)
        sys.exit(1)

    try:
        junit_data = parse_junit_xml(str(junit_path))
    except ET.ParseError as e:
        print(f"Error: failed to parse JUnit XML: {e}", file=sys.stderr)
        sys.exit(1)

    status = build_status(junit_data, args.commit, args.workflow)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w") as f:
        json.dump(status, f, indent=2)
        f.write("\n")

    # Print summary to stdout for CI logs
    print(f"CI Status: commit={status['commit'][:12]}  "
          f"tests={status['tests_total']}  "
          f"passed={status['passed']}  "
          f"failed={status['failed']}  "
          f"skipped={status['skipped']}  "
          f"gates={'PASS' if status['gates_passed'] else 'FAIL'}")
    print(f"Written to: {out_path}")


if __name__ == "__main__":
    main()
