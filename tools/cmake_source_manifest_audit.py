#!/usr/bin/env python3
"""Static CMake/source manifest census for Chronon3D.

This is deliberately a census tool, not a build-system authority. It reports:
  * source files under src/ and apps/ that are not mentioned by a CMake file;
  * literal C/C++/CUDA source references in CMake that do not exist.

Variable/generated references are ignored rather than guessed. The real build
remains the authority; use this before/alongside clean preset builds.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".cu"}
TOKEN_RE = re.compile(r"(?P<token>[^\s\"'()]+\.(?:c|cc|cpp|cxx|cu))(?=[\s\"')]|$)")


@dataclass(frozen=True)
class Reference:
    cmake_file: pathlib.Path
    token: str
    resolved: pathlib.Path | None


def cmake_files(root: pathlib.Path) -> list[pathlib.Path]:
    result = list(root.rglob("CMakeLists.txt"))
    result.extend(root.joinpath("cmake").rglob("*.cmake"))
    return sorted(set(result))


def source_files(root: pathlib.Path) -> set[pathlib.Path]:
    result: set[pathlib.Path] = set()
    for subtree in ("src", "apps"):
        base = root / subtree
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
                result.add(path.resolve())
    return result


def resolve_literal(root: pathlib.Path, cmake_file: pathlib.Path, token: str) -> pathlib.Path | None:
    if "$<" in token:
        return None
    expanded = token.replace("${CMAKE_SOURCE_DIR}", str(root))
    expanded = expanded.replace("${PROJECT_SOURCE_DIR}", str(root))
    expanded = expanded.replace("${CMAKE_CURRENT_SOURCE_DIR}", str(cmake_file.parent))
    if "${" in expanded or "$ENV{" in expanded:
        return None
    path = pathlib.Path(expanded)
    if not path.is_absolute():
        path = cmake_file.parent / path
    return path.resolve()


def collect_references(root: pathlib.Path) -> list[Reference]:
    refs: list[Reference] = []
    for cmake_file in cmake_files(root):
        try:
            text = cmake_file.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for match in TOKEN_RE.finditer(text):
            token = match.group("token")
            refs.append(Reference(cmake_file, token, resolve_literal(root, cmake_file, token)))
    return refs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--strict-unlisted", action="store_true",
                        help="also fail when a src/apps source has no literal CMake reference")
    args = parser.parse_args()
    root = args.root.resolve()

    sources = source_files(root)
    refs = collect_references(root)
    referenced = {ref.resolved for ref in refs if ref.resolved is not None}

    stale = sorted(
        (ref for ref in refs
         if ref.resolved is not None
         and ref.resolved.suffix.lower() in SOURCE_SUFFIXES
         and not ref.resolved.exists()),
        key=lambda ref: (str(ref.cmake_file), ref.token),
    )
    unlisted = sorted(sources - referenced)

    print(f"CMake files scanned: {len(cmake_files(root))}")
    print(f"Source files scanned: {len(sources)}")
    print(f"Literal source references: {len(referenced)}")

    if stale:
        print("\nStale literal CMake source references:")
        for ref in stale:
            print(f"  {ref.cmake_file.relative_to(root)}: {ref.token}")
    else:
        print("\nStale literal CMake source references: none")

    if unlisted:
        print("\nSources without a literal CMake reference (census candidates):")
        for path in unlisted:
            print(f"  {path.relative_to(root)}")
        print("Note: generated/variable-driven manifests may intentionally appear here.")
    else:
        print("\nSources without a literal CMake reference: none")

    if stale:
        return 1
    if args.strict_unlisted and unlisted:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
