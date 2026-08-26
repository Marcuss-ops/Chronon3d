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
        if any(part in {"build", "out", ".git", ".tmp", ".cache", "vcpkg_bootstrap", "vcpkg_installed"} for part in cmake_file.parts):
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


# ── Documented exceptions ──────────────────────────────────────────────────
# A source may deliberately be compiled into more than one CMake target.
# Each entry must name the EXACT resolved repo-relative path and the EXACT
# owner set; adding an entry requires a matching rationale comment.
#
# - src/backends/vulkan/cuda_vulkan_surface_bridge.cpp is compiled into BOTH
#   chronon3d_backend_vulkan (the real backend) AND the standalone probe
#   chronon3d_cuda_vulkan_external_memory_probe.  The probe is a diagnostic
#   executable that must exercise the bridge in isolation (no whole-backend
#   link), a deliberate probe/test-isolation compile of the same source.
ALLOWED_MULTI_OWNER: dict[tuple[str, ...], frozenset[str]] = {
    ("src", "backends", "vulkan", "cuda_vulkan_surface_bridge.cpp"): frozenset(
        {"chronon3d_backend_vulkan", "chronon3d_cuda_vulkan_external_memory_probe"}
    ),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", default=".", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    ownership = collect(root)
    conflicts = []
    for path, targets in ownership.items():
        if len(targets) <= 1:
            continue
        rel = path.relative_to(root)
        allowed = ALLOWED_MULTI_OWNER.get(rel.parts)
        if allowed is not None and frozenset(targets) == allowed:
            continue  # documented intentional dual-compile
        conflicts.append((path, sorted(targets)))
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
