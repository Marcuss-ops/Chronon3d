#!/usr/bin/env python3
"""Reject productive C++ sources assigned to more than one CMake target.

The check intentionally understands only the source-list forms used by this
repository: add_library(), add_executable(), and target_sources(). Generator
expressions and non-existent paths are ignored because they do not identify a
concrete source file at configure time.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path


COMMAND_RE = re.compile(r"\b(add_library|add_executable|target_sources)\s*\(", re.I)
KEYWORDS = {
    "STATIC", "SHARED", "MODULE", "OBJECT", "INTERFACE", "IMPORTED", "ALIAS",
    "WIN32", "MACOSX_BUNDLE", "EXCLUDE_FROM_ALL", "PRIVATE", "PUBLIC",
}


def strip_comments(text: str) -> str:
    text = re.sub(r"#[^\n]*", "", text)
    return text


def command_arguments(text: str, start: int) -> str:
    depth = 1
    i = start
    while i < len(text) and depth:
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
        i += 1
    if depth:
        raise ValueError("unclosed CMake command")
    return text[start:i - 1]


def tokens(arguments: str) -> list[str]:
    return re.findall(r'"([^"]+)"|([^\s]+)', arguments)


def flatten(matches: list[tuple[str, str]]) -> list[str]:
    return [quoted or bare for quoted, bare in matches]


def source_path(raw: str, cmake_dir: Path) -> Path | None:
    if "$<" in raw or "${" in raw or not raw.lower().endswith((".cpp", ".cxx", ".cc")):
        return None
    candidate = Path(raw)
    if not candidate.is_absolute():
        candidate = cmake_dir / candidate
    candidate = candidate.resolve()
    return candidate if candidate.is_file() else None


def collect(root: Path) -> dict[Path, set[str]]:
    ownership: dict[Path, set[str]] = defaultdict(set)
    for cmake_file in root.rglob("CMakeLists.txt"):
        if any(part in {"build", "out", ".git", "vcpkg_bootstrap", "vcpkg_installed"} for part in cmake_file.parts):
            continue
        text = strip_comments(cmake_file.read_text(encoding="utf-8"))
        for match in COMMAND_RE.finditer(text):
            args = flatten(tokens(command_arguments(text, match.end())))
            if not args:
                continue
            target = args[0]
            if match.group(1).lower() == "target_sources":
                source_args = args[1:]
            else:
                source_args = args[1:]
            for raw in source_args:
                if raw.upper() in KEYWORDS:
                    continue
                path = source_path(raw, cmake_file.parent)
                if path is not None:
                    ownership[path].add(target)
    return ownership


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", default=".", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    ownership = collect(root)
    conflicts = [(path, sorted(targets)) for path, targets in ownership.items() if len(targets) > 1]
    conflicts.sort(key=lambda item: str(item[0]))
    if conflicts:
        print(f"GATE_FAIL: {len(conflicts)} source(s) have multiple productive owners:")
        for path, targets in conflicts:
            print(f"  {path.relative_to(root)} -> {', '.join(targets)}")
        return 1
    print(f"GATE_PASS: {len(ownership)} concrete C++ source ownership entries are unique")
    return 0


if __name__ == "__main__":
    sys.exit(main())
