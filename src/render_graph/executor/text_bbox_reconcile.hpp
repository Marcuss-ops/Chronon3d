#pragma once

// ============================================================================
// text_bbox_reconcile.hpp — P0 #1 extracted reconcile function.
//
// Replaces the duplicate TU-local alpha-framebuffer scan + data-race-prone
// `static bool warned` warn-once in src/render_graph/executor/node_runner.cpp
// (the TICKET-SIMPLICITY-CONSERVATIVE-BBOX post-render alpha-bbox expansion,
// lines 383–431 of the original file).  The new function delegates to the
// canonical `chronon3d::alpha_bbox_scan()` (UNgated, no DIAGNOSTICS gate)
// instead of re-implementing the pixel walk inline.
//
// GATING: NONE.  This header is always compiled because the bbox-expansion
// logic is required for correctness in production builds — without it,
// unpredicted glyph ink would be clipped by downstream compositors.
//
// Anti-duplication: replaces the only prior duplication site of the alpha
// framebuffer walk.  Other bbox-expansion code lives in
// src/render_graph/nodes/TextRunNode.cpp (3 call sites) — out of scope for
// P0 #1; follow-up ticket TICKET-TEXT-BBOX-EXPAND-DEDUP will consolidate.
// ============================================================================

#include <optional>

namespace chronon3d {

class Framebuffer;           // fwd decl: `<chronon3d/core/memory/framebuffer.hpp>`
struct RenderCounters;       // fwd decl: `<chronon3d/core/profiling/render_counter_types.hpp>`

namespace raster {
struct BBox;                 // fwd decl: `<chronon3d/math/raster_utils.hpp>`
}

namespace graph {

class RenderGraphNode;       // fwd decl: `<chronon3d/render_graph/nodes/render_graph_node.hpp>`
struct TextBboxReporter;     // per-session reporter (text_bbox_reporter.hpp)

// ── reconcile_text_bbox_after_render ────────────────────────────────────────
// After a TextRun node renders, scan the rendered framebuffer's alpha channel
// via the canonical `alpha_bbox_scan()` and expand `predicted` if the actual
// ink extends beyond it.  Counter `text_bbox_contract_violations` is
// incremented (when `counters != nullptr`) on expansion; a single warn-once
// diagnostic is emitted per session via the provided `TextBboxReporter`
// (thread-safe; no static/process-wide state).
//
// Contract:
//   - `framebuffer` MUST be a valid reference to a sized framebuffer.
//     Caller is responsible for the null-check at the call site (matches
//     the original guard pattern in node_runner.cpp).
//   - `predicted` MUST be non-empty (caller-guarded).  An empty optional
//     yields a no-op return.
//   - `reporter` MUST outlive the function call.
//   - Returns std::optional<raster::BBox> carrying the EXPANDED bbox iff
//     expansion was needed.  std::nullopt on no-op paths (no ink found,
//     non-expanding match, or empty input).
//
// Step 2 fix preserved: the canonical scanner never early-exits (closes
// the historical `if (y > alpha_y1 + 2 && alpha_y1 >= alpha_y0) break;`
// bug that lost second lines, subtitles, and far-down descenders).
std::optional<raster::BBox> reconcile_text_bbox_after_render(
    const RenderGraphNode& node,
    const Framebuffer& framebuffer,
    const std::optional<raster::BBox>& predicted,
    RenderCounters* counters,
    TextBboxReporter& reporter);

} // namespace graph
} // namespace chronon3d
