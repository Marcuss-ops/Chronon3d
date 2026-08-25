#!/usr/bin/env python3
"""Enforce the core/feature dependency boundary in vcpkg.json."""
from __future__ import annotations

import argparse
import json
import pathlib
import sys


def package_name(dep: object) -> str:
    if isinstance(dep, str):
        return dep
    if isinstance(dep, dict) and isinstance(dep.get("name"), str):
        return dep["name"]
    raise ValueError(f"unsupported dependency entry: {dep!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=pathlib.Path, nargs="?", default=pathlib.Path("vcpkg.json"))
    ns = parser.parse_args()
    data = json.loads(ns.manifest.read_text(encoding="utf-8"))
    global_deps = {package_name(dep) for dep in data.get("dependencies", [])}
    features = data.get("features", {})
    feature_deps = {
        package_name(dep)
        for feature in features.values()
        for dep in feature.get("dependencies", [])
    }

    # These packages are intentionally feature-only.  Keeping the list in the
    # gate makes accidental promotion to the core graph a review-visible fail.
    feature_only = {
        "benchmark",
        "boost-math",
        "boost-spirit",
        "cli11",
        "cpptrace",
        "doctest",
        "efsw",
        "flatbuffers",
        "freetype",
        "fribidi",
        "harfbuzz",
        "icu",
        "libtess2",
        "msdfgen",
        "openexr",
        "opencolorio",
        "perfetto",
        "spirv-reflect",
        "spirv-tools",
        "vulkan",
        "vulkan-memory-allocator",
    }
    promoted = sorted(global_deps & feature_only)
    if promoted:
        print("DEPENDENCY_SCOPE_FAIL: feature-only packages are global:", file=sys.stderr)
        for name in promoted:
            print(f"  {name}", file=sys.stderr)
        return 1

    missing_feature_deps = sorted(name for name in feature_only if name in global_deps and name not in feature_deps)
    if missing_feature_deps:
        print("DEPENDENCY_SCOPE_FAIL: feature-only package has no feature owner:", file=sys.stderr)
        for name in missing_feature_deps:
            print(f"  {name}", file=sys.stderr)
        return 1

    print(
        f"DEPENDENCY_SCOPE_PASS: {len(global_deps)} core packages, "
        f"{len(feature_deps)} feature-owned package references"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
