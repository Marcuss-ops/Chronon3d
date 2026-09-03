#!/usr/bin/env python3
"""Architecture gate: CompiledResourceTable is the sole compiled resource authority.

P0.2 deliberately permits ResourcePlanner as an ephemeral planning algorithm,
but forbids reintroducing a persisted framebuffer allocation plan, a parallel
lifetime vector, or downstream production call sites that use the retired
physical_framebuffer_plan/lifetimes compatibility spellings.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CANONICAL_TABLE = ROOT / "include/chronon3d/render_graph/compiler/compiled_resource_table.hpp"
SCAN_ROOTS = (ROOT / "include", ROOT / "src")

FORBIDDEN_PATTERNS = (
    (
        re.compile(r"\b(?:struct|class)\s+PhysicalFramebufferAllocationPlan\b"),
        "retired PhysicalFramebufferAllocationPlan type",
    ),
    (
        re.compile(r"physical_framebuffer_allocation\.hpp"),
        "retired physical_framebuffer_allocation.hpp dependency",
    ),
    (
        re.compile(r"\bbuild_physical_framebuffer_allocation_plan\b"),
        "retired independent framebuffer allocation compiler",
    ),
    (
        re.compile(r"std::vector\s*<\s*ResourceLifetime\s*>\s+lifetimes\b"),
        "parallel compiled lifetime vector",
    ),
)

# Temporary zero-storage source-compatibility spellings may exist only inside
# the canonical table declaration. They must never leak back into production
# compiler/executor/backend call sites.
FORBIDDEN_OUTSIDE_CANONICAL = (
    (re.compile(r"\bphysical_framebuffer_plan\b"), "retired physical plan spelling"),
    (re.compile(r"\.lifetimes\b"), "retired direct lifetimes spelling"),
)


def production_files():
    for root in SCAN_ROOTS:
        for suffix in ("*.hpp", "*.h", "*.cpp", "*.cc", "*.cxx", "*.inc"):
            yield from root.rglob(suffix)


def main() -> int:
    failures: list[str] = []

    for path in sorted(set(production_files())):
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT).as_posix()

        for pattern, description in FORBIDDEN_PATTERNS:
            if pattern.search(text):
                failures.append(f"{rel}: {description}")

        if path != CANONICAL_TABLE:
            for pattern, description in FORBIDDEN_OUTSIDE_CANONICAL:
                if pattern.search(text):
                    failures.append(
                        f"{rel}: {description} — consume CompiledResourceTable directly"
                    )

    compiled_graph = ROOT / "include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp"
    if compiled_graph.exists():
        text = compiled_graph.read_text(encoding="utf-8", errors="replace")
        if "CompiledResourceTable" not in text or "resource_table()" not in text:
            failures.append(
                "include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp: "
                "compiled graph no longer exposes the canonical CompiledResourceTable boundary"
            )

    if failures:
        print("Compiled resource authority gate FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "Compiled resource authority gate passed: CompiledResourceTable is the sole "
        "persisted lifetime/allocation authority."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
