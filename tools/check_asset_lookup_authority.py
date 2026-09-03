#!/usr/bin/env python3
"""P0.0 audit: core code must not resolve assets relative to the process CWD."""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCAN_ROOTS = (ROOT / "include", ROOT / "src")
SUFFIXES = ("*.hpp", "*.h", "*.cpp", "*.cc", "*.cxx", "*.inc")

FORBIDDEN_NAMES = ("camera_reference.jpg", "Poppins-Bold.ttf")
CWD_ASSET_LITERAL = re.compile(
    r"(?:u8|u|U|L)?(?:R)?[\"'](?:\./)?assets[/\\\\]",
    re.IGNORECASE,
)


def production_files():
    for root in SCAN_ROOTS:
        for suffix in SUFFIXES:
            yield from root.rglob(suffix)


def main() -> int:
    failures: list[str] = []
    for path in sorted(set(production_files())):
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT).as_posix()
        for name in FORBIDDEN_NAMES:
            if name in text:
                failures.append(f"{rel}: retired hard-coded asset {name!r}")
        if CWD_ASSET_LITERAL.search(text):
            failures.append(
                f"{rel}: CWD-relative assets/... lookup — use AssetResolver/registry authority"
            )

    if failures:
        print("Asset lookup authority gate FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "Asset lookup authority gate passed: no retired hard-coded asset names or "
        "CWD-relative assets/... literals exist in core production code."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
