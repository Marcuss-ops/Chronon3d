#!/usr/bin/env python3
"""Verify the installed authoring header closure is complete and disjoint."""

from __future__ import annotations

import re
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
CORE_MANIFEST = ROOT / "cmake" / "Chronon3DPublicHeaders.cmake"
SDK_TARGETS = ROOT / "cmake" / "Chronon3DSdkTargets.cmake"
AUTHORING_MANIFEST = ROOT / "cmake" / "Chronon3DAuthoringPublicHeaders.cmake"
INCLUDE_ROOT = ROOT / "include"

MANIFEST_ENTRY = re.compile(
    r'"\$\{CMAKE_SOURCE_DIR\}/include/(chronon3d/[^"\n]+)"'
)
PUBLIC_INCLUDE = re.compile(
    r'^\s*#\s*include\s*<((?:chronon3d)/[^>]+)>', re.MULTILINE
)
RELATIVE_INCLUDE = re.compile(
    r'^\s*#\s*include\s*"([^"\n]+)"', re.MULTILINE
)


def manifest_paths(path: Path) -> set[str]:
    return set(MANIFEST_ENTRY.findall(path.read_text(encoding="utf-8")))


def normalized_relative_include(owner: str, included: str) -> str | None:
    """Resolve a quoted include inside include/ without touching the filesystem."""
    if included.startswith("chronon3d/"):
        candidate = PurePosixPath(included)
    else:
        candidate = PurePosixPath(owner).parent / included

    parts: list[str] = []
    for part in candidate.parts:
        if part in ("", "."):
            continue
        if part == "..":
            if not parts:
                return None
            parts.pop()
        else:
            parts.append(part)
    return "/".join(parts)


def main() -> int:
    core = manifest_paths(CORE_MANIFEST)
    sdk_fragments = manifest_paths(SDK_TARGETS)
    authoring_additions = manifest_paths(AUTHORING_MANIFEST)
    established = core | sdk_fragments
    installed = established | authoring_additions

    failures: list[str] = []

    failures.extend(
        f"duplicate FILE_SET membership: {relative}"
        for relative in sorted(established & authoring_additions)
    )

    audited = {
        relative
        for relative in installed
        if relative.startswith("chronon3d/authoring/")
    }
    audited.update(authoring_additions)

    for relative in sorted(audited):
        source = INCLUDE_ROOT / relative
        if not source.is_file():
            failures.append(f"manifest entry does not exist: include/{relative}")
            continue

        text = source.read_text(encoding="utf-8", errors="replace")
        for included in PUBLIC_INCLUDE.findall(text):
            if included not in installed:
                failures.append(
                    f"include/{relative} includes <{included}> but it is absent "
                    "from every explicit public FILE_SET declaration"
                )

        for included in RELATIVE_INCLUDE.findall(text):
            resolved = normalized_relative_include(relative, included)
            if resolved is None:
                failures.append(
                    f"include/{relative} has escaping relative include \"{included}\""
                )
            elif resolved.startswith("chronon3d/") and resolved not in installed:
                failures.append(
                    f"include/{relative} includes \"{included}\" -> {resolved}, "
                    "absent from every explicit public FILE_SET declaration"
                )

    required = {
        "chronon3d/authoring/asset.hpp",
        "chronon3d/authoring/layer.hpp",
        "chronon3d/authoring/text.hpp",
        "chronon3d/sdk/render_engine.hpp",
    }
    failures.extend(
        f"documented public header missing: {relative}"
        for relative in sorted(required - installed)
    )

    if failures:
        print("[AUTHORING-HEADER-CLOSURE-FAIL]", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        f"[AUTHORING-HEADER-CLOSURE-OK] {len(authoring_additions)} focused additions; "
        f"{len(audited)} authoring headers audited; "
        f"{len(installed)} total explicit public headers; 0 duplicates"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
