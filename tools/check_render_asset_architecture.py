#!/usr/bin/env python3
"""Fail-loud audit for unified rendering and per-runtime asset ownership."""

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

ASSET_IMPLEMENTATION_ROOTS = (
    ROOT / "include" / "chronon3d" / "assets",
    ROOT / "src" / "assets",
    ROOT / "src" / "runtime",
    ROOT / "src" / "backends",
)

COMPOSITION_ROOTS = (ROOT / "content", ROOT / "examples")

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


def asset_resolution_files() -> list[Path]:
    """Return files that can materially participate in asset path resolution."""
    selected: list[Path] = []
    for path in source_files(ASSET_IMPLEMENTATION_ROOTS):
        lowered_parts = {part.lower() for part in path.parts}
        lowered_name = path.name.lower()
        asset_named = any(
            token in lowered_name or token in lowered_parts
            for token in ("asset", "font", "image")
        )
        runtime_owner = lowered_name in {
            "render_engine.cpp",
            "render_runtime.cpp",
            "render_runtime.hpp",
        }
        if asset_named or runtime_owner:
            selected.append(path)
    return selected


def strip_comments_and_literals(text: str) -> str:
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
        cleaned = strip_comments_and_literals(
            path.read_text(encoding="utf-8", errors="replace")
        )
        for match in pattern.finditer(cleaned):
            failures.append(
                f"{path.relative_to(ROOT)}:{line_number(cleaned, match.start())}: "
                f"{label}: {match.group(0)}"
            )
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
            asset_resolution_files(),
            re.compile(r"\b(?:(?:std::)?filesystem::)?current_path\s*\("),
            "asset resolution must not consult process CWD",
        )
    )

    # Match ownership/construction shapes, not harmless type mentions in docs,
    # function parameters or resolver adapters.
    resolver_construction = re.compile(
        r"\b(?:chronon3d::)?(?:assets::)?AssetResolver\s+[A-Za-z_]\w*\s*[;={]"
        r"|\bnew\s+(?:chronon3d::)?(?:assets::)?AssetResolver\b"
        r"|\bstd::make_(?:unique|shared)<\s*(?:chronon3d::)?(?:assets::)?AssetResolver\s*>"
        r"|\b(?:chronon3d::)?(?:assets::)?AssetResolver\s*\{"
    )
    failures.extend(
        scan_pattern(
            source_files(COMPOSITION_ROOTS),
            resolver_construction,
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
