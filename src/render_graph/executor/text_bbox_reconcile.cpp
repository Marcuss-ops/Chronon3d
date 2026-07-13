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

    // ── Determine the actual ink bbox with a three-tier fallback ─────────
    // 1. Prefer the intrinsic bbox produced by the rasterizer (no scan).
    // 2. Fall back to the predicted bbox (already includes effect padding).
    // 3. Last resort: canonical full-framebuffer alpha-bbox scan.
    raster::BBox actual;
    bool have_actual = false;

    if (actual_ink_bbox && !actual_ink_bbox->is_empty()) {
        actual = *actual_ink_bbox;
        have_actual = true;
    } else if (predicted && !predicted->is_empty()) {
        actual = *predicted;
        have_actual = true;
    } else {
        const Rect ink_rect = alpha_bbox_scan(framebuffer);
        if (ink_rect.size.x > 0.0f && ink_rect.size.y > 0.0f) {
            actual = raster::BBox{
                static_cast<i32>(ink_rect.origin.x),
                static_cast<i32>(ink_rect.origin.y),
                static_cast<i32>(ink_rect.origin.x) + static_cast<i32>(ink_rect.size.x),
                static_cast<i32>(ink_rect.origin.y) + static_cast<i32>(ink_rect.size.y),
            };
            have_actual = true;
        }
    }

    if (!have_actual) {
        return std::nullopt;
    }

    // ── Expand the predicted bbox if ink exceeded prediction on any side ──
    raster::BBox expanded = *predicted;
    bool needs_expand = false;
    if (actual.x0 < expanded.x0) { expanded.x0 = actual.x0; needs_expand = true; }
    if (actual.y0 < expanded.y0) { expanded.y0 = actual.y0; needs_expand = true; }
    if (actual.x1 > expanded.x1) { expanded.x1 = actual.x1; needs_expand = true; }
    if (actual.y1 > expanded.y1) { expanded.y1 = actual.y1; needs_expand = true; }

    if (!needs_expand) {
        return std::nullopt;  // prediction was already correct — no work to do.
    }

    // ── Counter increment (profiling; always incremented regardless of
    //    the warn-once state) ────────────────────────────────────────────
    if (counters) {
        counters->text_bbox_contract_violations.fetch_add(
            1, std::memory_order_relaxed);
    }

    // ── Warn-once diagnostic (per-session; no static state) ────────────────
    // Logged values: ORIGINAL predicted (semantically: what the producer
    // claimed), ACTUAL ink bbox (what the scanner measured), and EXPANDED
    // bbox (the conservative union used downstream).  Previous TU-local
    // code logged the expanded bbox TWICE in the `predicted=` and
    // `expanded=` fields (because `predicted_bbox = expanded;` had
    // destructively mutated the variable before the message assembly).
    // That confused output is now correct — the original prediction is
    // preserved in the function-local `*predicted` reference.
    if (reporter.mark_warned()) {
        spdlog::warn(
            "[text-bbox] POST_RENDER_EXPAND node={} "
            "predicted=({}, {}, {}, {}) "
            "actual=({}, {}, {}, {}) "
            "expanded=({}, {}, {}, {})",
            node.name(),
            predicted->x0, predicted->y0, predicted->x1, predicted->y1,
            actual.x0, actual.y0, actual.x1, actual.y1,
            expanded.x0, expanded.y0, expanded.x1, expanded.y1);
    }

    return std::optional<raster::BBox>(expanded);
}

} // namespace chronon3d::graph
