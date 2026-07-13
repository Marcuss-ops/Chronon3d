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
    TextBboxReporter& reporter)
{
    // ── Pre-conditions (caller should have guarded but we re-check for
    //    defensive consistency with the function contract). ───────────────
    if (!predicted || predicted->is_empty()) {
        return std::nullopt;
    }
    if (framebuffer.width() <= 0 || framebuffer.height() <= 0) {
        return std::nullopt;
    }

    // ── Canonical alpha-bbox scan (the SINGLE source of truth) ────────────
    const Rect ink_rect = alpha_bbox_scan(framebuffer);
    // Empty Rect ⇒ no visible ink ⇒ no expansion possible / needed.
    // The canonical scanner returns `Rect{}` (default-constructed ⇒ origin
    // {0,0}, size {0,0}) when no pixel exceeds `kAlphaBBoxScanThreshold`,
    // OR when the framebuffer is zero-sized.  Both cases bail here.
    if (ink_rect.size.x <= 0.0f || ink_rect.size.y <= 0.0f) {
        return std::nullopt;
    }

    // ── Convert Rect → raster::BBox ────────────────────────────────────────
    // The canonical scanner returns:
    //   Rect{ origin = {alpha_x0, alpha_y0},
    //         size   = {alpha_x1 - alpha_x0 + 1, alpha_y1 - alpha_y0 + 1} }
    // The original (now removed) inline code returned the equivalent
    //   raster::BBox actual{alpha_x0, alpha_y0, alpha_x1 + 1, alpha_y1 + 1};
    // i.e. raster::BBox's x1/y1 are EXCLUSIVE (one past the last ink pixel).
    // We restore that exact semantic via `x1_b = origin.x + size.x`.
    // Float ↔ i32 rounding: `size.x` originates from integer arithmetic in
    // alpha_bbox_scanner.cpp (so it is an exact float representable as
    // a non-negative integer whenever the framebuffer dimensions fit in
    // 2^24, which is true for any practical canvas).
    const raster::BBox actual{
        static_cast<i32>(ink_rect.origin.x),
        static_cast<i32>(ink_rect.origin.y),
        static_cast<i32>(ink_rect.origin.x) + static_cast<i32>(ink_rect.size.x),
        static_cast<i32>(ink_rect.origin.y) + static_cast<i32>(ink_rect.size.y),
    };

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
