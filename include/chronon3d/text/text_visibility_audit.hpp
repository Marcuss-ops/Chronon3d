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
#include <cstdint>
#include <functional>                           // std::hash for TextWarningKey
#include <string_view>                          // stable node-name hash
#include <unordered_set>                        // WarnOnceDeduper backing store

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

/// §9 FU04 — `TextVisibilityStatus` enum, the single-source-of-truth PASS/FAIL
/// verdict for the text-bbox contract. Computed from the 4 critical invariants
/// (font_resolved, shaping_succeeded, finite, predicted_contains_world) plus
/// the 2 deferred alpha-bbox invariants (clip_contains_visible_ink, alpha_bbox
/// non-empty — only evaluated when a framebuffer is supplied).
///
/// - `PASS`: all 4 critical invariants hold AND either (a) no framebuffer was
///   provided (alpha-bbox invariants deferred) OR (b) framebuffer was provided
///   AND clip_contains_visible_ink is true.
/// - `FAIL`: at least one critical invariant is false, OR framebuffer was
///   provided AND clip_contains_visible_ink is false.
/// - `INDETERMINATE`: legacy sentinel for "audit not yet evaluated" (default
///   value of `status` field). Reserved for future use; not produced by
///   `audit_text_visibility()`.
enum class TextVisibilityStatus : u8 {
    INDETERMINATE = 0,  // not yet evaluated (default-initialized struct)
    PASS           = 1,  // all critical invariants hold
    FAIL           = 2,  // at least one critical invariant violated
};

/// §12 FU09 / TICKET-SIMPLICITY-VISIBILITY-CONTRACT dedup taxonomy.
/// One value per diagnostic invariant emitted by `verify_text_visibility()`.
/// Used together with `TextWarningKey::node_id` to dedup warnings per
/// session — replacing the prior 6 `static bool` one-shot pattern (which
/// had process-wide state, hidden globals, and data races in parallel
/// render paths).
///
/// All values are stable; new entries may be appended at the end of the
/// range without breaking the F1.E regression lock (existing callers
/// never construct TextWarningKey directly — they pass through the
/// `make_warning_key()` canonical helper in text_visibility_audit.cpp).
enum class TextWarningKind : u8 {
    FontUnresolved    = 1,  // §1.E asset-1: shape.engine == nullptr
    ShapingFailed     = 2,  // §1.E asset-2: layout.placed.glyphs.empty()
    BboxNonFinite     = 3,  // §1.E math-3:  any of 5 bboxes has NaN/Inf
    PredictedTooSmall = 4,  // §1.E math-4:  predicted_bbox ⊉ world_ink_bbox
    ClipDropsInk      = 5,  // §1.E math-5:  clip_rect ⊉ rendered_alpha_bbox
    NoInkRendered     = 6,  // §1.E assset-6: rendered alpha_bbox empty
};

/// `TextWarningKey` — composite key for the warn-once deduper. The
/// `(node_id, kind)` pair uniquely identifies one diagnostic stream per
/// node per invariant. Stable hash via `std::hash<TextWarningKey>`
/// specialization below (Cat-3 anti-duplication: we do NOT introduce a
/// generic string-key deduper; the enum+int pair is the minimal surface
/// for the audit contract).
struct TextWarningKey {
    int             node_id;  // -1 for unnamed nodes; otherwise stable hash
    TextWarningKind kind;
    bool operator==(const TextWarningKey& other) const noexcept {
        return node_id == other.node_id && kind == other.kind;
    }
};

} // namespace chronon3d

namespace std {
template<>
struct hash<chronon3d::TextWarningKey> {
    std::size_t operator()(const chronon3d::TextWarningKey& k) const noexcept {
        // XOR-shift combine: node_id dominates because each node has its own
        // diagnostic stream; kind disambiguates the 6 invariants within a node.
        return static_cast<std::size_t>(k.node_id)
             ^ (static_cast<std::size_t>(k.kind) << 1);
    }
};
} // namespace std

namespace chronon3d {

/// `WarnOnceDeduper` — session-scoped warn-once deduplicator. Caller
/// instantiates one per auditable scope (per-render-graph, per-composition
/// evaluation, per-CLI-invocation) and passes it to `verify_text_visibility()`.
/// Each `(node_id, kind)` key is emitted at most once per deduper lifetime.
///
/// AGENTS.md Cat-5 compliance: NOT a singleton. NOT a global. Class-level
/// state is per-instance, plain `std::unordered_set` backing store, zero
/// hidden state outside the explicit instance.
///
/// Replace site: the prior 6 `static bool wN = false;` one-shots which
/// had 3 known §honesty defects (process-wide hidden state, data race
/// on parallel render, first-error masks later nodes). All three are
/// closed by the per-instance dedup model: each render call gets its
/// own deduper OR shares one via a contract-documented owner (e.g.
/// `static WarnOnceDeduper s_deduper;` at TU scope, or a class member).
class WarnOnceDeduper {
public:
    /// Returns `true` iff this is the FIRST time `key` is observed by this
    /// deduper instance. After `true` is returned once for a given key,
    /// ALL subsequent calls with the same key return `false`. Caller
    /// pattern: `if (deduper.mark_warned(key)) spdlog::warn(...);`
    bool mark_warned(const TextWarningKey& key) {
        return warned_.insert(key).second;
    }
    void  clear()                       noexcept { warned_.clear(); }
    std::size_t size()            const noexcept { return warned_.size(); }
    bool  empty()                const noexcept { return warned_.empty(); }

private:
    std::unordered_set<TextWarningKey> warned_;
};

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
///
/// §9 FU04 — added `status` (PASS/FAIL), `should_disable_tile_pruning` (FU04
/// violation response flag), and `expanded_predicted_bbox` (FU04 violation
/// response output: world_ink_bbox expanded by effect_padding).
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

    // ── §9 FU04 — status + violation response ──────────────────────
    TextVisibilityStatus status{TextVisibilityStatus::INDETERMINATE};
    // PASS if all 4 critical invariants (font_resolved, shaping_succeeded,
    // finite, predicted_contains_world) hold AND (no framebuffer OR
    // clip_contains_visible_ink). FAIL otherwise. See `audit_text_visibility`
    // for the exact cascade.

    bool should_disable_tile_pruning{false};
    // True iff `predicted_contains_world` is false. Set as the FU04
    // violation response flag: the caller (TextRunNode::predicted_bbox)
    // reads this and applies the bbox expansion (see below). For TextRun
    // nodes tile_pruning is already off by design (see
    // tile_pruning::compute_dirty_clip), so the "disable" semantics is
    // realized via the bbox expansion that guarantees rasterization of
    // all visible ink.

    Rect expanded_predicted_bbox{};
    // FU04 violation response output: `world_ink_bbox` expanded by
    // `effect_padding` on all 4 sides. ONLY valid when
    // `should_disable_tile_pruning == true`. Zero-rect (default) when
    // the audit PASSes. Caller reads this when the flag is set and
    // substitutes it for the original tight `predicted_bbox`.
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
///   - `effect_padding`: §9 FU04 violation response input — the radius
///     (in canvas pixels) used to expand `world_ink_bbox` when
///     `predicted_contains_world` is false. Typically the text's
///     shadow/glow spread (see `TextRunNode::predicted_bbox`'s `spread`
///     computation). Default 0.0f for backwards-compat with FU02 call
///     sites that don't supply the parameter.
///
/// Returns by value; the caller reads the populated struct.
///
/// No globals. No side effects. The hot path performs no allocations;
/// the fallback path for shapes without animated glyph instances may
/// allocate a small `std::vector<GlyphInstanceState>`. Marked
/// `[[nodiscard]]` so callers cannot accidentally drop the audit.
[[nodiscard]] TextVisibilityAudit audit_text_visibility(
    const TextRunShape& shape,
    const Mat4&         world_matrix,
    const Rect&         predicted_bbox,
    const Rect&         clip_rect,
    const Framebuffer*  rendered_output = nullptr,
    float               effect_padding  = 0.0f
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
///   5. clip_contains_visible_ink — expand(clip_rect, kTextAuditBBoxTolerance) ⊇ rendered_alpha_bbox
///   6. rendered_alpha_bbox non-empty — actual ink pixels detected
///
/// Returns the populated `TextVisibilityAudit` so callers can also
/// inspect the result programmatically.
///
/// DIAGNOSTIC DEDUP: warnings are emitted at most once per
/// `(node_id, TextWarningKind)` pair per `deduper` lifetime. The deduper
/// is owned by the caller — pass an instance shared across the auditable
/// scope (e.g. per-render-graph or per-composition evaluation). The prior
/// `static bool` pattern (process-wide state, data-race on parallel
/// render, first-error masking) is removed; the deduper instance model
/// closes all three defects.
///
/// @param shape             The TextRunShape with layout, engine, glyphs
/// @param world_matrix      Layer → canvas transform matrix
/// @param predicted_bbox    Producer-supplied predicted bbox (canvas-level)
/// @param clip_rect         Compositor clip rect (canvas-level)
/// @param rendered_output   Optional framebuffer for alpha-bbox scan
/// @param node_name         Diagnostic label for log messages
/// @param deduper           Per-scope warn-once deduper (replaces static bool)
/// @return Populated TextVisibilityAudit struct
[[nodiscard]] TextVisibilityAudit verify_text_visibility(
    const TextRunShape& shape,
    const Mat4&         world_matrix,
    const Rect&         predicted_bbox,
    const Rect&         clip_rect,
    const Framebuffer*  rendered_output,
    const char*         node_name,
    WarnOnceDeduper&    deduper,
    float               effect_padding = 0.0f
);

} // namespace chronon3d

#endif // CHRONON3D_BUILD_DIAGNOSTICS
