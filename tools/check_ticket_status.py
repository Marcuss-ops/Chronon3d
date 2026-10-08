#!/usr/bin/env python3
"""Cross-check ticket status claims against the test registry.

For every docs/tickets/*.md:
  * read its status ("Stato" / "Status") line;
  * collect every backticked test target name matching chronon3d_*_tests;
  * verify each cited target is registered in some tests/*.cmake file.

A ticket that claims DONE but cites a target that is not registered is
reported as STALE. Exit code 0 when no stale claim is found, 1 otherwise, so
the checker can gate CI. Tickets with no status line are reported as UNKNOWN
and do not fail the check.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TICKETS = ROOT / "docs" / "tickets"
CMAKE_TESTS = ROOT / "tests"

STATUS_RE = re.compile(r"^(?:\*\*)?(?:Stato|Status)(?:\*\*)?\s*:?\s*(.*)$", re.IGNORECASE | re.MULTILINE)
TARGET_RE = re.compile(r"`(chronon3d_[a-z0-9_]+_tests)`")
REGISTERED_RE = re.compile(r"NAME\s+(chronon3d_[a-z0-9_]+_tests)")


def registered_targets() -> set[str]:
    found: set[str] = set()
    for cmake in CMAKE_TESTS.rglob("*.cmake"):
        found.update(REGISTERED_RE.findall(cmake.read_text(encoding="utf-8", errors="replace")))
    for cmake in CMAKE_TESTS.glob("*.txt"):
        found.update(REGISTERED_RE.findall(cmake.read_text(encoding="utf-8", errors="replace")))
    return found


def classify(status_text: str) -> str:
    upper = status_text.upper()
    if "DONE" in upper and "NOT" not in upper:
        return "DONE"
    if "VERIFY" in upper or "IMPLEMENTED" in upper or "ATTERRAT" in upper:
        return "VERIFY"
    if "OPEN" in upper or "NOT STARTED" in upper or "DEFERRED" in upper:
        return "OPEN"
    return "UNKNOWN"


def main() -> int:
    registry = registered_targets()
    stale: list[str] = []
    rows: list[tuple[str, str, int, int]] = []
    for ticket in sorted(TICKETS.glob("*.md")):
        text = ticket.read_text(encoding="utf-8", errors="replace")
        match = STATUS_RE.search(text)
        status = classify(match.group(1)) if match else "UNKNOWN"
        cited = set(TARGET_RE.findall(text))
        missing = sorted(t for t in cited if t not in registry)
        rows.append((ticket.name, status, len(cited), len(missing)))
        if status == "DONE" and missing:
            stale.append(f"{ticket.name}: DONE but cites unregistered {', '.join(missing)}")
    for name, status, cited, missing in rows:
        print(f"{status:8} cited={cited:2} unregistered={missing:2}  {name}")
    print(f"registered test targets: {len(registry)}")
    if stale:
        print("STALE CLAIMS:")
        for line in stale:
            print("  " + line)
        return 1
    print("no stale DONE claims")
    return 0


if __name__ == "__main__":
    sys.exit(main())
