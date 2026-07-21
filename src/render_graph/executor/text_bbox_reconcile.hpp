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

#include <chronon3d/math/raster_utils.hpp>  // raster::BBox full def

#include <optional>

namespace chronon3d {

class Framebuffer;           // fwd decl: `<chronon3d/core/memory/framebuffer.hpp>`
struct RenderCounters;       // fwd decl: `<chronon3d/core/profiling/render_counter_types.hpp>`

namespace graph {

class RenderGraphNode;       // fwd decl: `<chronon3d/render_graph/nodes/render_graph_node.hpp>`
struct TextBboxReporter;     // per-session reporter (text_bbox_reporter.hpp)

// ── reconcile_text_bbox_after_render ────────────────────────────────────────
// Kept for ABI compatibility, but the POST_RENDER_EXPAND fallback has been
// removed.  The predicted bbox is now the single source of truth and is
// computed from FreeType outline bboxes (see src/backends/text/font_engine.cpp
// and src/text/text_run_geometry.cpp), which already account for glyph ink
// extents including negative side-bearings and descenders.
//
// Contract:
//   - All parameters are accepted for backward compatibility but are ignored.
//   - Always returns std::nullopt.
//
// The legacy alpha-scan + expansion code has been intentionally removed so
// that under-sized predicted bboxes are surfaced at their source rather than
// masked by a post-render expansion.
std::optional<raster::BBox> reconcile_text_bbox_after_render(
    const RenderGraphNode& node,
    const Framebuffer& framebuffer,
    const std::optional<raster::BBox>& predicted,
    RenderCounters* counters,
    TextBboxReporter& reporter,
    const std::optional<raster::BBox>& actual_ink_bbox = std::nullopt);

} // namespace graph
} // namespace chronon3d
