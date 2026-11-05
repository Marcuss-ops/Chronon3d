#!/usr/bin/env python3
"""Cross-check ticket status claims against the test registry.

For every docs/tickets/*.md:
  * read its status ("Stato" / "Status") line;
  * collect every backticked test target name matching chronon3d_*_tests;
  * verify each cited target is registered in some tests/*.cmake file.

A ticket that claims CLOSED/DONE but cites a target that is not registered is
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

STATUS_RE = re.compile(r"^\s*(?:\*\*)?(?:Stato|Status)(?:\*\*)?\s*:?\s*(.*)$", re.IGNORECASE)
BULLET_STATUS_RE = re.compile(r"^\s*[-*]\s+(?:\*\*)?(?:Stato|Status)(?:\*\*)?\s*:\s*(.*)$", re.IGNORECASE)
TABLE_STATUS_RE = re.compile(r"^\s*\|\s*\*{0,2}(?:Stato|Status)\*{0,2}\s*\|\s*(.+?)\s*\|", re.IGNORECASE)
HEADING_STATUS_RE = re.compile(r"^#{1,6}\s*(Stato|Status)\s*$", re.IGNORECASE)
TARGET_RE = re.compile(r"`(chronon3d_[a-z0-9_]+_tests)`")
REGISTERED_RE = re.compile(r"NAME\s+(chronon3d_[a-z0-9_]+_tests)")


def registered_targets() -> set[str]:
    found: set[str] = set()
    for cmake in CMAKE_TESTS.rglob("*.cmake"):
        found.update(REGISTERED_RE.findall(cmake.read_text(encoding="utf-8", errors="replace")))
    for cmake in CMAKE_TESTS.glob("*.txt"):
        found.update(REGISTERED_RE.findall(cmake.read_text(encoding="utf-8", errors="replace")))
    return found


def declared_status(text: str) -> str:
    """Read the sheet-level status from the first lines, including bullets
    and the ``## Stato`` heading followed by its value."""
    lines = text.splitlines()[:15]
    for index, line in enumerate(lines):
        if HEADING_STATUS_RE.match(line):
            for following in lines[index + 1 : index + 4]:
                if following.strip():
                    return following
            return ""
        match = (
            STATUS_RE.match(line)
            or BULLET_STATUS_RE.match(line)
            or TABLE_STATUS_RE.match(line)
        )
        if match:
            return match.group(1)
    return ""


def classify(status_text: str) -> str:
    cleaned = re.sub(r"[`*|]", "", status_text).strip()
    head = re.split(r"[\s\u2014\u2013\-:;.]+", cleaned, maxsplit=1)[0].lower()
    if head in {
        "closed", "done", "chiuso", "chiusa", "chiusi", "chiuse",
        "archiviato", "archiviata", "complete", "completed",
    }:
        return "CLOSED"
    if head in {"verify", "implemented", "atterrato", "atterrata"}:
        return "VERIFY"
    if head in {"open", "deferred", "blocked", "proposed"}:
        return "OPEN"
    return "UNKNOWN"


def main() -> int:
    registry = registered_targets()
    stale: list[str] = []
    rows: list[tuple[str, str, int, int]] = []
    for ticket in sorted(TICKETS.glob("*.md")):
        text = ticket.read_text(encoding="utf-8", errors="replace")
        status = classify(declared_status(text))
        cited = set(TARGET_RE.findall(text))
        missing = sorted(t for t in cited if t not in registry)
        rows.append((ticket.name, status, len(cited), len(missing)))
        if status == "CLOSED" and missing:
            stale.append(f"{ticket.name}: CLOSED but cites unregistered {', '.join(missing)}")
    for name, status, cited, missing in rows:
        print(f"{status:8} cited={cited:2} unregistered={missing:2}  {name}")
    print(f"registered test targets: {len(registry)}")
    if stale:
        print("STALE CLAIMS:")
        for line in stale:
            print("  " + line)
        return 1
    print("no stale CLOSED/DONE claims")
    return 0


if __name__ == "__main__":
    sys.exit(main())
