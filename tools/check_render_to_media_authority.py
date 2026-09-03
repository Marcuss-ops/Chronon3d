#!/usr/bin/env python3
"""P1 audit: RenderToMedia stays graph-derived and receipt/finalization remain canonical."""

from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MEDIA = ROOT / "include/chronon3d/media/render_to_media.hpp"
COMPILED = ROOT / "include/chronon3d/render_graph/compiler/compiled_frame_graph.hpp"
SERIALIZER = ROOT / "include/chronon3d/render_graph/compiler/compiled_frame_graph_serializer.hpp"
SINK = ROOT / "include/chronon3d/runtime/output_sink.hpp"


def require(path: pathlib.Path, fragments: tuple[str, ...], failures: list[str]) -> str:
    if not path.exists():
        failures.append(f"{path.relative_to(ROOT)}: missing")
        return ""
    text = path.read_text(encoding="utf-8", errors="replace")
    for fragment in fragments:
        if fragment not in text:
            failures.append(f"{path.relative_to(ROOT)}: missing contract {fragment!r}")
    return text


def main() -> int:
    failures: list[str] = []
    media = require(MEDIA, (
        "struct MediaSurfaceDesc",
        "using ColorDescription = runtime::FrameFormat",
        "enum class ZeroCopyPolicy",
        "class RenderToMediaResolver",
        "CompiledResourceTable& table",
        "ResourceSubresource::Plane0",
        "ResourceSubresource::Plane1",
        "ZeroCopyProof::Proven",
    ), failures)
    require(COMPILED, (
        "std::optional<::chronon3d::media::RenderToMediaPlan> render_to_media",
        "PassQueryArena query_arena",
        "operation.pass_timing = query_arena.allocate()",
    ), failures)
    require(SERIALIZER, (
        "serialize_compiled_frame_graph(",
        "serialize_compiled_frame_graph_dot(",
        "physical_allocations",
        "dependencies",
        "make_render_receipt(",
        "receipt.output = sink.finalize()",
        "receipt.pass_timings",
        "receipt.total_gpu_time_ns",
    ), failures)
    sink = require(SINK, (
        "OutputSinkMode::AppendOnly",
        "OutputSinkMode::Seekable",
        "hash_final_content_impl()",
        "finalize()",
    ), failures)

    for forbidden in (
        "release_after_level",
        "release_schedule",
        "ResourceStateTracker",
        "std::vector<runtime::ResourceTransition>",
        "std::vector<ResourceTransition>",
    ):
        if forbidden in media:
            failures.append(f"include/chronon3d/media/render_to_media.hpp: parallel authority {forbidden!r}")

    if "if (m_mode == OutputSinkMode::Seekable)" not in sink:
        failures.append("output_sink.hpp: seekable final-content hashing branch is missing")

    if failures:
        print("RenderToMedia/finalization authority gate FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "RenderToMedia/finalization authority gate passed: media decisions derive from "
        "CompiledResourceTable and receipt hashing is finalized by OutputSink."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
