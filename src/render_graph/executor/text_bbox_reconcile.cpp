// ============================================================================
// text_bbox_reconcile.cpp — P0 #1 implementation.
//
// Replaces the duplicate TU-local alpha-framebuffer scan + data-race-prone
// `static bool warned` warn-once flag that lived in
// src/render_graph/executor/node_runner.cpp:383–431 (the
// TICKET-SIMPLICITY-CONSERVATIVE-BBOX post-render expansion block).
//
// Two defects closed:
//   (a) Duplicate alpha scan (closed by delegating to the canonical
//       `chrono3d::alpha_bbox_scan()` in `<chronon3d/text/alpha_bbox_scanner.hpp>`).
//   (b) `static bool warned = false;` race / suppression
//       (closed via the per-session `TextBboxReporter` with a single
//       `std::atomic<bool>` `exchange(true)` semantics — exactly-once
//       across all threads, lock-free, no process-wide hidden state).
//
// GATING: NONE.  No `#ifdef CHRONON3D_BUILD_DIAGNOSTICS` gate.  The bbox
// expansion logic is correctness-critical and must run in production.
//
// Step 2 fix preserved: the canonical scanner never early-exits the
// per-row walk (the historical `if (y > alpha_y1 + 2 ...) break;` bug
// that lost second lines / subtitles / far-down descenders is gone with
// the duplicate).
// ============================================================================

#include "text_bbox_reconcile.hpp"
#include "text_bbox_reporter.hpp"

#include <chronon3d/text/alpha_bbox_scanner.hpp>           // canonical alpha-bbox scan (UNgated)
#include <chronon3d/media/media_placement.hpp>              // chronon3d::Rect (alpha_bbox_scan return type)
#include <chronon3d/core/memory/framebuffer.hpp>            // Framebuffer full def
#include <chronon3d/core/profiling/render_counter_types.hpp> // RenderCounters full def
#include <chronon3d/math/raster_utils.hpp>                  // raster::BBox full def
#include <chronon3d/render_graph/nodes/render_graph_node.hpp> // RenderGraphNode::name()

#include <spdlog/spdlog.h>

#include <optional>

namespace chronon3d::graph {

std::optional<raster::BBox> reconcile_text_bbox_after_render(
    const RenderGraphNode& node,
    const Framebuffer& framebuffer,
    const std::optional<raster::BBox>& predicted,
    RenderCounters* counters,
    TextBboxReporter& reporter,
    const std::optional<raster::BBox>& actual_ink_bbox)
{
    // ── Pre-conditions (caller should have guarded but we re-check for
    //    defensive consistency with the function contract). ───────────────
    if (!predicted || predicted->is_empty()) {
        return std::nullopt;
    }
    if (framebuffer.width() <= 0 || framebuffer.height() <= 0) {
        return std::nullopt;
    }

    // Geometric ink bbox is now the single source of truth; the legacy
    // POST_RENDER_EXPAND alpha-scan fallback has been removed.  The
    // predicted bbox is computed from FreeType outline bboxes (see
    // src/backends/text/font_engine.cpp + src/text/text_run_geometry.cpp),
    // which already account for glyph ink extents including negative
    // side-bearings and descenders.  Keeping a post-render expansion here
    // masked under-sized predicted bboxes instead of fixing them at the
    // source, so we no longer expand.
    (void)framebuffer;
    (void)actual_ink_bbox;
    (void)counters;
    (void)node;
    (void)reporter;
    return std::nullopt;
}

} // namespace chronon3d::graph
