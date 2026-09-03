#!/usr/bin/env python3
"""Architecture gate: CompiledResourceTable is the sole compiled resource authority.

P0.2 deliberately permits ResourcePlanner as an ephemeral placement algorithm,
but forbids reintroducing any persisted framebuffer allocation plan, parallel
lifetime/consumer authority, or source-compatibility shim from the retired
compiled model.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CANONICAL_TABLE = ROOT / "include/chronon3d/render_graph/compiler/compiled_resource_table.hpp"
COMPILED_GRAPH = ROOT / "include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp"
SCAN_ROOTS = (ROOT / "include", ROOT / "src")

# Match actual retired compiled-resource declarations/includes/compiler APIs.
# ResourcePlanner/ResourcePlan remain valid ephemeral placement machinery; their
# result is lowered into CompiledResourceTable and is not a second persisted
# compiled authority.
FORBIDDEN_PATTERNS = (
    (
        re.compile(r"\b(?:struct|class)\s+PhysicalFramebufferAllocationPlan\b"),
        "retired PhysicalFramebufferAllocationPlan type",
    ),
    (
        re.compile(
            r"^\s*#\s*include\s*[<\"](?:[^>\"]*/)?physical_framebuffer_allocation\.hpp[>\"]",
            re.MULTILINE,
        ),
        "retired physical_framebuffer_allocation.hpp dependency",
    ),
    (
        re.compile(r"\bbuild_physical_framebuffer_allocation_plan\s*\("),
        "retired independent framebuffer allocation compiler",
    ),
    (
        re.compile(r"std::vector\s*<\s*ResourceLifetime\s*>\s+lifetimes\b"),
        "parallel compiled lifetime vector",
    ),
    (
        re.compile(r"std::vector\s*<\s*std::size_t\s*>\s+consumer_counts\b"),
        "parallel compiled consumer-count vector",
    ),
    (
        re.compile(r"\bphysical_framebuffer_plan\b"),
        "retired physical plan compatibility spelling",
    ),
    (
        re.compile(r"(?:\.|->)\s*lifetimes\b"),
        "retired direct lifetimes compatibility access",
    ),
    (
        re.compile(r"(?:\.|->)\s*consumer_counts\b"),
        "retired direct consumer-count compatibility access",
    ),
    (
        re.compile(r"\b(?:CompiledResourceRecord|ResourceLifetime)\b"),
        "retired compiled resource compatibility alias",
    ),
    (
        re.compile(r"\bkInvalidPhysicalFramebufferSlot\b"),
        "retired framebuffer-slot sentinel spelling",
    ),
)

# The canonical compiled table used to carry zero-storage shims that were safe
# from a storage perspective but kept the retired API alive. P0.2 is complete
# only while those spellings stay absent from the authority itself. Note that
# runtime::ResourcePlan::allocation_for() is intentionally valid; only the old
# CompiledResourceTable compatibility method is forbidden here.
FORBIDDEN_CANONICAL_PATTERNS = (
    (re.compile(r"\bphysical_framebuffer_plan\b"), "physical_framebuffer_plan shim"),
    (re.compile(r"\blifetimes\b"), "lifetimes shim"),
    (re.compile(r"\bCompiledResourceRecord\b"), "CompiledResourceRecord alias"),
    (re.compile(r"\bResourceLifetime\b"), "ResourceLifetime alias"),
    (re.compile(r"\bkInvalidPhysicalFramebufferSlot\b"), "legacy framebuffer-slot sentinel"),
    (re.compile(r"\ballocation_for\s*\("), "compiled allocation_for compatibility API"),
)


def production_files():
    for root in SCAN_ROOTS:
        for suffix in ("*.hpp", "*.h", "*.cpp", "*.cc", "*.cxx", "*.inc"):
            yield from root.rglob(suffix)


def main() -> int:
    failures: list[str] = []

    if not CANONICAL_TABLE.exists():
        failures.append(
            "include/chronon3d/render_graph/compiler/compiled_resource_table.hpp: "
            "canonical CompiledResourceTable is missing"
        )
    else:
        canonical_text = CANONICAL_TABLE.read_text(encoding="utf-8", errors="replace")
        for pattern, description in FORBIDDEN_CANONICAL_PATTERNS:
            if pattern.search(canonical_text):
                failures.append(
                    "include/chronon3d/render_graph/compiler/compiled_resource_table.hpp: "
                    f"retired compatibility surface returned ({description})"
                )

        required_fragments = (
            "struct CompiledResourcePlan",
            "struct CompiledResourceTable",
            "std::vector<CompiledResourcePlan> resources",
            "std::vector<runtime::PhysicalResourceSlot> slots",
            "resource_for(",
            "release_schedule(",
        )
        for fragment in required_fragments:
            if fragment not in canonical_text:
                failures.append(
                    "include/chronon3d/render_graph/compiler/compiled_resource_table.hpp: "
                    f"canonical authority contract missing {fragment!r}"
                )

    for path in sorted(set(production_files())):
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT).as_posix()

        for pattern, description in FORBIDDEN_PATTERNS:
            if pattern.search(text):
                failures.append(
                    f"{rel}: {description} — consume CompiledResourceTable directly"
                )

    if not COMPILED_GRAPH.exists():
        failures.append(
            "include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp: "
            "compiled graph boundary is missing"
        )
    else:
        graph_text = COMPILED_GRAPH.read_text(encoding="utf-8", errors="replace")
        if "CompiledResourceTable" not in graph_text or "resource_table()" not in graph_text:
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
        "Compiled resource authority gate passed: P0.2 is sealed; CompiledResourceTable "
        "is the sole persisted compiled lifetime/allocation/consumer authority and no "
        "compatibility shims remain."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
