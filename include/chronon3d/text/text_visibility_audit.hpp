// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_visibility_audit.hpp
//
// FU02 of `docs/tickets/TICKET-TEXT-VISIBILITY-PIPELINE.md`.
//
// Single-source-of-truth for the text-bbox / visibility contract. Replaces
// the ad-hoc visibility checks previously scattered across:
//   - `src/render_graph/nodes/TextRunNode.cpp::predicted_bbox` (bbox contract)
//   - `src/render_graph/executor/tile_pruning.hpp::compute_dirty_clip`
//   - `tests/text_golden/text_clip/text_completeness.cpp` (assertion surface)
//   - `apps/chronon3d_cli/commands/text_inspect/` (FU09 forward-point)
//   - `src/runtime/telemetry/` TEXT-AUDIT rows (FU10 forward-point)
//   - `src/render_graph/preflight/` for property-based fuzz (FU11 forward-point)
//
// =========================================================================
// AGENTS.md v0.1 freeze compliance
// =========================================================================
// Cat-3 (NEW PUBLIC API): zero new public API surface unless
//                          `CHRONON3D_BUILD_DIAGNOSTICS=ON`. The entire
//                          content of this header is gated by
//                          `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`, so a
//                          production SDK build without diagnostics
//                          produces zero symbols from this header
//                          (the canonical Cat-3 zero-surface invariant).
// Cat-5 (NEW SINGLETON):  zero — `TextVisibilityAudit` is a plain
//                          aggregate returned by value from a pure
//                          function. `audit_text_visibility()` has no
//                          global state, no registry, no cache, no
//                          singleton. The `Rect`/`Mat4`/`TextRunShape`
//                          types it touches are existing canonical
//                          types from `media_placement.hpp`,
//                          `glm_types.hpp`, `text_run_shape.hpp`
//                          (zero new infrastructure).
// Cat-7 (DENY HEADERS):   zero `#include <msdfgen>|<libtess2>|<unicode[/...]>`
//                          added. The struct uses `cstddef` + pre-existing
//                          project types only.
//
// Lifetime: stateless, returns by value, marked `[[nodiscard]]`. The
// caller passes all inputs explicitly; no hidden dependencies.
// ═══════════════════════════════════════════════════════════════════════════

#ifdef CHRONON3D_BUILD_DIAGNOSTICS

#include <cstddef>

#include <chronon3d/media/media_placement.hpp>   // chronon3d::Rect
#include <chronon3d/math/glm_types.hpp>          // chronon3d::Mat4

namespace chronon3d {

class TextRunShape;     // forward decl — full definition lives in
                        // `<chronon3d/text/text_run_shape.hpp>`. The audit
                        // function only reads `engine`, `cache`, and the
                        // immutable `layout.placed.glyphs` size, so a
                        // forward declaration is sufficient at compile
                        // time. Callers that trigger the actual function
                        // include the full header.
class Framebuffer;      // forward decl — `<chronon3d/core/memory/framebuffer.hpp>`.
                        // Only width()/height()/pixel() are touched.

/// 4-D coordinate tolerance for the `predicted_bbox ⊇ world_ink_bbox`
/// containment invariant. Matches the roadmap ticket's `kBBoxTolerance=2.0f`
/// (FU04 spec) for sub-pixel rasterization rounding.
///
/// Mirrors ADR-019 Decision 5 (predicted_bbox MUST use the same matrix chain
/// as rendering) — the tolerance guards against float-rounding artifacts in
/// the world_bbox projection, NOT against real geometry drift. A violation
/// beyond ±2 px is a true contract fault.
inline constexpr float kTextAuditBBoxTolerance = 2.0f;

/// `TextVisibilityAudit` — single-source-of-truth contract struct for
/// text-bbox / visibility / clipping invariants. Populated by
/// `audit_text_visibility()` and consumed by:
///
/// 1. `TextRunNode::predicted_bbox` (FU03 call-site wiring — replaces the
///    ad-hoc empty/non-finite guard added in FU01)
/// 2. `tile_pruning::compute_dirty_clip` (FU08 cross-pipeline parity)
/// 3. `tests/text_golden/text_clip/text_completeness.cpp` — every TEST_CASE
///    gets a new `CHECK(audit.predicted_contains_world)`
///    + `CHECK(audit.clip_contains_visible_ink)` (FU06 extension)
/// 4. `chronon3d_cli inspect-text` JSON subcommand (FU09) per-node dump
/// 5. Per-node `TEXT-AUDIT` telemetry rows (FU10) when
///    `ctx.policy.diagnostics_enabled == true`
/// 6. Property-based fuzz (FU11) for the seed-sweep regression-lock matrix
///
/// The fields are intentionally split per ADR-019 Decision 2: every
/// bbox-producing function declares its coordinate level.
struct TextVisibilityAudit {
    // ── font + shaping stage ────────────────────────────────────────────
    bool        font_resolved{false};       // `shape.engine != nullptr`
                                            // (the raw FontEngine* is wired
                                            // and shapeable). NOTE: not a
                                            // dual AND-gate with `cache`,
                                            // because `cache` is the
                                            // per-shape TextLayoutCache used
                                            // for re-shape optimisation, NOT
                                            // a precondition for font
                                            // resolution. PR 9 staged the
                                            // layout-cache as optional so
                                            // text shapes can render without
                                            // a cache (tests re-shape on
                                            // every frame).  See
                                            // `text_run_shape.hpp` for the
                                            // `engine` vs `cache` contract.
    bool        shaping_succeeded{false};   // `shape.layout.placed.glyphs`
                                            // is non-empty after text shaping.
    std::size_t glyph_count{0};             // `shape.layout.placed.glyphs.size()`
                                            // — exact count for invariants.

    // ── bbox pipeline (4 distinct levels per ADR-019 D2) ─────────────────
    Rect local_ink_bbox{};                  // Text-frame-relative axes-aligned
                                            // ink bbox (Glyph → Box level).
    Rect world_ink_bbox{};                  // `transform_aabb(local_ink_bbox,
                                            //  world_matrix)` (Box → Layer).
    Rect predicted_bbox{};                  // Caller-provided (typically from
                                            // `TextRunNode::predicted_bbox()`)
                                            // (Layer → Canvas).
    Rect clip_rect{};                       // Caller-provided (compositor clip).
    Rect rendered_alpha_bbox{};             // Measured alpha-bbox of the
                                            // rasterized output (only valid
                                            // when `rendered_output != nullptr`);
                                            // zero-rect if unscanned.

    // ── containment + sanity invariants ──────────────────────────────────
    bool finite{false};                     // All 5 bboxes have finite
                                            // coordinates (no NaN, no Inf).
                                            // FU01's `std::isfinite` check
                                            // generalized here.
    bool predicted_contains_world{false};   // `expand(predicted_bbox,
                                            //  kTextAuditBBoxTolerance) ⊇
                                            //  world_ink_bbox` (FU04 invariant
                                            //  pre-clip guard).
    bool clip_contains_visible_ink{false};  // ONLY meaningful when
                                            //  `rendered_output != nullptr`.
                                            //  When False ⇒ the compositor
                                            //  either:
                                            //    (a) was not given a
                                            //        framebuffer to scan
                                            //        (NOT measured), or
                                            //    (b) measured an alpha
                                            //        bbox that does NOT
                                            //        overlap the clip rect.
                                            //  The default `false` preserves
                                            //  the safe semantics: callers
                                            //  honouring the contract will
                                            //  check `rendered_alpha_bbox`
                                            //  BEFORE consuming this field.
                                            //  The smoking-gun invariant for
                                            //  the Clip 06 19-pixel sliver
                                            //  regression.
};

/// `audit_text_visibility()` — single canonical pure function.
///
/// Inputs:
///   - `shape`: the `TextRunShape` carrying layout, glyph states, engine.
///   - `world_matrix`: layer → canvas transform.
///   - `predicted_bbox`: producer-supplied bbox (typically from
///     `TextRunNode::predicted_bbox()`).
///   - `clip_rect`: compositor clip rect (canvas-level).
///   - `rendered_output`: optional `Framebuffer*`. When non-null the
///     alpha-bbox is measured; when nullptr only the math side is checked.
///
/// Returns by value; the caller reads the populated struct.
///
/// No globals. No side effects. No allocations beyond the small fixed-size
/// return value (Rect × 5 + bool × 5 + size_t). Marked `[[nodiscard]]` so
/// callers cannot accidentally drop the audit.
[[nodiscard]] TextVisibilityAudit audit_text_visibility(
    const TextRunShape& shape,
    const Mat4&         world_matrix,
    const Rect&         predicted_bbox,
    const Rect&         clip_rect,
    const Framebuffer*  rendered_output = nullptr
);

/// `verify_text_visibility()` — F1.E post-render visibility contract.
///
/// Calls `audit_text_visibility()` and emits structured `spdlog::warn`
/// diagnostics for each of the 6 invariants that fail:
///
///   1. font_resolved        — `shape.engine != nullptr`
///   2. shaping_succeeded    — `shape.layout.placed.glyphs.size() > 0`
///   3. finite               — all 5 bboxes have finite coordinates
///   4. predicted_contains_world — predicted_bbox ⊇ world_ink_bbox
///   5. clip_contains_visible_ink — clip_rect ∩ rendered_alpha_bbox ≠ ∅
///   6. rendered_alpha_bbox non-empty — actual ink pixels detected
///
/// Returns the populated `TextVisibilityAudit` so callers can also
/// inspect the result programmatically.
///
/// GATE: one-shot `spdlog::warn` per invariant (static bool pattern)
/// to prevent log spam on repeated violations.
///
/// @param shape             The TextRunShape with layout, engine, glyphs
/// @param world_matrix      Layer → canvas transform matrix
/// @param predicted_bbox    Producer-supplied predicted bbox (canvas-level)
/// @param clip_rect         Compositor clip rect (canvas-level)
/// @param rendered_output   Optional framebuffer for alpha-bbox scan
/// @param node_name         Diagnostic label for log messages
/// @return Populated TextVisibilityAudit struct
[[nodiscard]] TextVisibilityAudit verify_text_visibility(
    const TextRunShape& shape,
    const Mat4&         world_matrix,
    const Rect&         predicted_bbox,
    const Rect&         clip_rect,
    const Framebuffer*  rendered_output,
    const char*         node_name
);

} // namespace chronon3d

#endif // CHRONON3D_BUILD_DIAGNOSTICS
