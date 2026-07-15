#!/usr/bin/env python3
"""Fail-loud source audit for the unified render and per-runtime asset contracts.

This gate intentionally checks architecture, not behaviour:
- retired render planners/executors must not return;
- process-wide/global asset roots must not return;
- asset resolution code must not consult the process CWD;
- compositions/examples must not construct their own AssetResolver;
- retired root-level umbrella headers must remain absent.

Comments and string literals are stripped before identifier matching so historical
notes and user-facing diagnostics do not create false positives.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}

PRODUCTION_ROOTS = (
    ROOT / "include" / "chronon3d",
    ROOT / "src",
    ROOT / "apps" / "chronon3d_cli",
    ROOT / "content",
    ROOT / "examples",
)

ASSET_RESOLUTION_ROOTS = (
    ROOT / "include" / "chronon3d" / "assets",
    ROOT / "src" / "assets",
    ROOT / "src" / "runtime",
    ROOT / "src" / "backends" / "assets",
    ROOT / "src" / "backends" / "image",
    ROOT / "src" / "backends" / "software",
)

COMPOSITION_ROOTS = (
    ROOT / "content",
    ROOT / "examples",
)

BANNED_IDENTIFIERS = (
    "RenderJobPlan",
    "VideoJobPlan",
    "execute_video_job",
    "command_still",
    "command_video",
    "process_wide_assets_root",
    "set_process_wide_assets_root",
    "global_asset_root",
    "set_global_assets_root",
)

RETIRED_UMBRELLAS = (
    ROOT / "include" / "chronon3d" / "chronon3d.hpp",
    ROOT / "include" / "chronon3d" / "authoring.hpp",
    ROOT / "include" / "chronon3d" / "render.hpp",
    ROOT / "include" / "chronon3d" / "advanced.hpp",
)


def source_files(roots: tuple[Path, ...]) -> list[Path]:
    files: list[Path] = []
    for root in roots:
        if not root.exists():
            continue
        files.extend(
            path
            for path in root.rglob("*")
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
        )
    return sorted(set(files))


def strip_comments_and_literals(text: str) -> str:
    """Replace C/C++ comments and quoted literals with whitespace."""
    pattern = re.compile(
        r"//[^\n]*"
        r"|/\*.*?\*/"
        r"|R\"(?P<delim>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delim)\""
        r"|\"(?:\\.|[^\"\\])*\""
        r"|'(?:\\.|[^'\\])*'",
        re.DOTALL,
    )
    return pattern.sub(lambda match: "\n" * match.group(0).count("\n"), text)


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def scan_pattern(files: list[Path], pattern: re.Pattern[str], label: str) -> list[str]:
    failures: list[str] = []
    for path in files:
        raw = path.read_text(encoding="utf-8", errors="replace")
        cleaned = strip_comments_and_literals(raw)
        for match in pattern.finditer(cleaned):
            relative = path.relative_to(ROOT)
            failures.append(f"{relative}:{line_number(cleaned, match.start())}: {label}: {match.group(0)}")
    return failures


def main() -> int:
    failures: list[str] = []
    production_files = source_files(PRODUCTION_ROOTS)

    for identifier in BANNED_IDENTIFIERS:
        failures.extend(
            scan_pattern(
                production_files,
                re.compile(rf"\b{re.escape(identifier)}\b"),
                "retired identifier",
            )
        )

    failures.extend(
        scan_pattern(
            source_files(ASSET_RESOLUTION_ROOTS),
            re.compile(r"\b(?:(?:std::)?filesystem::)?current_path\s*\("),
            "asset resolution must not consult process CWD",
        )
    )

    failures.extend(
        scan_pattern(
            source_files(COMPOSITION_ROOTS),
            re.compile(r"\bAssetResolver\b"),
            "compositions/examples must not construct or own AssetResolver",
        )
    )

    for umbrella in RETIRED_UMBRELLAS:
        if umbrella.exists():
            failures.append(
                f"{umbrella.relative_to(ROOT)}: retired umbrella header must remain absent"
            )

    if failures:
        print("[RENDER-ASSET-ARCH-FAIL] unified architecture regression detected", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "[RENDER-ASSET-ARCH-OK] no legacy planners/executors, global asset roots, "
        "CWD asset fallbacks, composition-owned resolvers, or retired umbrellas"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
