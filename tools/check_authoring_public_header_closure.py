#!/usr/bin/env python3
"""Verify the focused authoring FILE_SET is complete and globally disjoint."""

from __future__ import annotations

import re
import sys
from pathlib import Path

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


def manifest_paths(path: Path) -> set[str]:
    return set(MANIFEST_ENTRY.findall(path.read_text(encoding="utf-8")))


def main() -> int:
    core = manifest_paths(CORE_MANIFEST)
    sdk_fragments = manifest_paths(SDK_TARGETS)
    authoring = manifest_paths(AUTHORING_MANIFEST)
    established = core | sdk_fragments
    installed = established | authoring

    failures: list[str] = []

    duplicates = sorted(established & authoring)
    failures.extend(
        f"duplicate FILE_SET membership: {relative}"
        for relative in duplicates
    )

    for relative in sorted(authoring):
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
        f"[AUTHORING-HEADER-CLOSURE-OK] {len(authoring)} focused additions; "
        f"{len(installed)} total explicit public headers; 0 duplicates"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
