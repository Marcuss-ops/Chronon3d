#!/usr/bin/env python3
"""Phase-1 architecture gate: one image-format/color taxonomy authority.

The canonical declarations live in include/chronon3d/runtime/frame_format.hpp.
Media/cache/backend code may use aliases or FrameFormat values, but must not
re-declare competing enum/metadata types.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CANONICAL = ROOT / "include/chronon3d/runtime/frame_format.hpp"
SCAN_ROOTS = (ROOT / "include", ROOT / "src")

FORBIDDEN_ENUMS = (
    "EncoderPixelFormat",
    "VideoPixelFormat",
    "YuvMatrix",
    "ColorRange",
    "PixelFormat",
)


def production_files():
    for root in SCAN_ROOTS:
        for suffix in ("*.hpp", "*.h", "*.cpp", "*.cc", "*.cxx"):
            yield from root.rglob(suffix)


def main() -> int:
    failures: list[str] = []
    enum_patterns = {
        name: re.compile(rf"\benum\s+(?:class\s+)?{re.escape(name)}\b")
        for name in FORBIDDEN_ENUMS
    }
    color_metadata_struct = re.compile(r"\bstruct\s+ColorMetadata\b")

    for path in sorted(set(production_files())):
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT).as_posix()

        for name, pattern in enum_patterns.items():
            if not pattern.search(text):
                continue
            if path == CANONICAL and name in {"PixelFormat", "ColorRange"}:
                continue
            failures.append(
                f"{rel}: duplicate enum authority '{name}' — use runtime::FrameFormat taxonomy"
            )

        if path != CANONICAL and color_metadata_struct.search(text):
            failures.append(
                f"{rel}: duplicate ColorMetadata struct — carry runtime::FrameFormat instead"
            )

    surface_header = ROOT / "include/chronon3d/runtime/render_surface.hpp"
    if surface_header.exists():
        text = surface_header.read_text(encoding="utf-8", errors="replace")
        if re.search(r"\bColorMetadata\s+color\s*[;{]", text):
            failures.append(
                "include/chronon3d/runtime/render_surface.hpp: SurfaceDesc still has a color side-channel"
            )

    if failures:
        print("Canonical frame-format gate FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("Canonical frame-format gate passed: runtime::FrameFormat is the sole production authority.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
