#!/usr/bin/env python3
"""Verify the explicit authoring FILE_SET has a complete public include closure."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE_MANIFEST = ROOT / "cmake" / "Chronon3DPublicHeaders.cmake"
AUTHORING_MANIFEST = ROOT / "cmake" / "Chronon3DAuthoringPublicHeaders.cmake"
INCLUDE_ROOT = ROOT / "include"

MANIFEST_ENTRY = re.compile(
    r'"\$\{CMAKE_SOURCE_DIR\}/include/(chronon3d/[^"\n]+)"'
)
PUBLIC_INCLUDE = re.compile(r'^\s*#\s*include\s*<((?:chronon3d)/[^>]+)>', re.MULTILINE)


def manifest_paths(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    return set(MANIFEST_ENTRY.findall(text))


def main() -> int:
    core = manifest_paths(CORE_MANIFEST)
    authoring = manifest_paths(AUTHORING_MANIFEST)
    installed = core | authoring

    failures: list[str] = []
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
                    "from both public FILE_SET manifests"
                )

    # Lock the four documented starter includes into the installed subset.
    required = {
        "chronon3d/authoring/asset.hpp",
        "chronon3d/authoring/layer.hpp",
        "chronon3d/authoring/text.hpp",
        "chronon3d/sdk/render_engine.hpp",
    }
    missing_required = sorted(required - installed)
    failures.extend(f"documented public header missing: {path}" for path in missing_required)

    if failures:
        print("[AUTHORING-HEADER-CLOSURE-FAIL]", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        f"[AUTHORING-HEADER-CLOSURE-OK] {len(authoring)} authoring headers; "
        f"{len(installed)} total explicit public headers"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
