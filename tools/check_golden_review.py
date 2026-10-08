#!/usr/bin/env python3
"""Gate for golden fixtures (*.golden) under tests/.

Process (docs/GOLDEN_REVIEW.md):
  * every golden starts with a comment header that says when it may be
    regenerated (a line starting with "# Regenerate");
  * every golden records its last intentional change in a dated line
    ("# YYYY-MM-DD") and its review state in a "# Review:" line;
  * a golden whose value changed relative to git HEAD must carry a dated
    change entry newer than HEAD's entry.

Exit status:
  1  structural problem (missing header, missing Review line, changed value
     without a dated change entry)
  0  structurally sound. Goldens whose review state is not "reviewed" are
     listed as UNREVIEWED: they pass the gate but are reported, not hidden.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATE_RE = re.compile(r"^#.*\b(\d{4}-\d{2}-\d{2})\b")
REVIEW_RE = re.compile(r"^#\s*Review:\s*(.+)$", re.MULTILINE)


def goldens() -> list[Path]:
    return sorted(p for p in (ROOT / "tests").rglob("*.golden") if p.is_file())


def head_text(path: Path) -> str | None:
    rel = path.relative_to(ROOT).as_posix()
    result = subprocess.run(["git", "show", f"HEAD:{rel}"], cwd=ROOT,
                            capture_output=True, text=True, check=False)
    return result.stdout if result.returncode == 0 else None


def latest_date(text: str) -> str:
    dates = [m.group(1) for line in text.splitlines() if (m := DATE_RE.match(line))]
    return max(dates) if dates else ""


def check(path: Path) -> tuple[list[str], str]:
    errors: list[str] = []
    text = path.read_text(encoding="utf-8")
    rel = path.relative_to(ROOT).as_posix()

    if not text.startswith("#"):
        errors.append(f"{rel}: missing comment header")
    if not any(line.startswith("# Regenerate") for line in text.splitlines()):
        errors.append(f"{rel}: header lacks a '# Regenerate ...' line")
    review = REVIEW_RE.search(text)
    if not review:
        errors.append(f"{rel}: missing '# Review:' line")
        state = "missing"
    else:
        state = review.group(1).strip()

    before = head_text(path)
    if before is not None and before != text:
        value_before = [l for l in before.splitlines() if l and not l.startswith("#")]
        value_now = [l for l in text.splitlines() if l and not l.startswith("#")]
        if value_before != value_now and latest_date(text) <= latest_date(before):
            errors.append(f"{rel}: value changed without a newer dated change entry")
    return errors, state


def main() -> int:
    files = goldens()
    if not files:
        print("no golden fixtures found")
        return 0
    failures: list[str] = []
    unreviewed: list[str] = []
    for path in files:
        errors, state = check(path)
        failures.extend(errors)
        if not state.lower().startswith("reviewed"):
            unreviewed.append(f"{path.relative_to(ROOT).as_posix()} (review: {state})")
    for line in failures:
        print("FAIL", line)
    for line in unreviewed:
        print("UNREVIEWED", line)
    print(f"{len(files)} golden(s): {len(failures)} structural failure(s), {len(unreviewed)} unreviewed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
