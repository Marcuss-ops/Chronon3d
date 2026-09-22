#!/usr/bin/env python3
"""Guard the focused text-header migration and print its consumer census."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

SKIP = {".git", "build", "out", "__pycache__"}
SUFFIXES = {".h", ".hh", ".hpp", ".cc", ".cpp", ".cxx"}
MAX_BROAD_CONSUMERS = 22


def source_files(root: Path):
    for base_name in ("include", "src", "apps", "tests"):
        base = root / base_name
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix in SUFFIXES:
                if not any(part in SKIP or part.startswith("build-") for part in path.parts):
                    yield path


def consumers(root: Path, header: str) -> list[str]:
    pattern = re.compile(
        r"#\s*include\s*[<\"](?:chronon3d/)?[^>\"]*"
        + re.escape(header) + r"[>\"]"
    )
    return sorted(
        path.relative_to(root).as_posix()
        for path in source_files(root)
        if pattern.search(path.read_text(encoding="utf-8", errors="replace"))
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    root = parser.parse_args().root.resolve()
    focused = (
        "text_unicode_utf8.hpp",
        "text_unicode_grapheme.hpp",
        "text_font_resources.hpp",
        "text_resource_contracts.hpp",
        "text_scratch_pool.hpp",
    )
    missing = [
        name for name in focused
        if not (root / "include/chronon3d/backends/text" / name).exists()
    ]
    if missing:
        print("TEXT_HEADER_GATE_FAIL: missing focused header(s): " + ", ".join(missing))
        return 1

    broad = consumers(root, "text_render_resources.hpp")
    unicode_facade = consumers(root, "text_unicode_utils.hpp")
    facade = "include/chronon3d/backends/text/text_unicode_utils.hpp"
    unexpected = [name for name in unicode_facade if name != facade]
    print(f"text_render_resources_direct_consumers={len(broad)}")
    for name in broad:
        print(f"  broad: {name}")
    print(f"text_unicode_utils_direct_consumers={len(unicode_facade)}")
    for name in unicode_facade:
        print(f"  unicode: {name}")
    if unexpected:
        print("TEXT_HEADER_GATE_FAIL: unicode compatibility facade has consumers:")
        for name in unexpected:
            print(f"  {name}")
        return 1
    if len(broad) > MAX_BROAD_CONSUMERS:
        print(
            "TEXT_HEADER_GATE_FAIL: text_render_resources.hpp fan-out "
            f"{len(broad)} exceeds ratchet {MAX_BROAD_CONSUMERS}"
        )
        return 1
    print(f"text_render_resources_fanout_ratchet={MAX_BROAD_CONSUMERS}")
    print("TEXT_HEADER_GATE_PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
