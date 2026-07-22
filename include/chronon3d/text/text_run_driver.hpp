// SPDX-License-Identifier: MIT
#pragma once
//
// text_run_driver.hpp — Per-frame driver that wires TextRunLayout (via
// TextRunShape) into the compositor and animation pipeline (PR 8).
//
// Until PR 7 the AE-style text animator stack was evaluated exactly once
// when a scene was built; the resulting per-glyph states were stored on
// `TextRunShape::glyphs` and then FROZEN for the run's lifetime.  PR 8
// adds two free functions that break that freeze:
//
//   update_text_run_shape_per_frame(shape, time)
//     Cheap.  Re-evaluates `shape.animators` writing results back into
//     `shape.glyphs`.  No re-shaping.  Invoked once per node execution
//     in the TextRunNode's executor path; the cached TextRunNode key
//     already folds `hash_text_run_shape(*shape)` so any glyph
//     mutation invalidates the cache correctly.
//
//   apply_active_state_to_text_run_shape(shape, state, engine, layout)
//     Bridges `AnimatedTextDocument` into the text pipeline.  Rebuilds
//     `shape->layout` (and re-seeds `shape.glyphs`) when the active
//     document or transition_text changes, using `build_text_run` and
//     the shared `TextLayoutCache`.  Used by the scene-side builder
//     when an AnimatedTextDocument is attached to a layer.
//
// Color/font styling (paint, material, shadows) is preserved across
// layout swaps — only the immutable layout slot is swapped, and the
// mutable glyphs vector is re-seeded from the new placed run.
//
// M1.5#2 — added `EffectiveTextState` (text + FontLayoutIdentity +
// direction + language + features) so the fast-path in
// `apply_active_state_to_text_run_shape` participates in EVERY cache
// dimension of `build_text_run`, not just text + font.  Pre-M1.5#2 the
// fast-path silently missed direction/language/features flips — the
// cache key folded them correctly, so a direction-flip produced a
// cache MISS in `build_text_run` even when the shape's `layout` slot
// was not rebuilt, paying a full HarfBuzz pass on every frame.

#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>

namespace chronon3d {

// ──────────────────────────────────────────────────────────────────────────
// M1.5#2 — EffectiveTextState: canonical per-frame text identity.
// ──────────────────────────────────────────────────────────────────────────
//
// The fast-path in `apply_active_state_to_text_run_shape` compares the
// shape's CURRENT layout state against the TARGET state derived from
// the `ActiveTextState` sample.  If they're equal, the driver skips
// `build_text_run` (cache-hit).  EffectiveTextState bundles the five
// fields that participate in `TextLayoutCacheKey::digest()` so the
// fast-path sees the SAME identity the cache uses.  Adding a missing
// dimension here = a false cache hit on `build_text_run`.
//
// Pre-M1.5#2 the comparison was source_text + FontLayoutIdentity only
// (P0-2 fix); direction/language/features were silently ignored even
// though the cache key folds them (TICKET-103a colligation).  The
// regression lock in `tests/text/test_effective_text_state.cpp`
// verifies all 5 dimensions participate.

struct EffectiveTextState {
    std::string            text;
    FontLayoutIdentity     font;
    TextDirection          direction{TextDirection::Auto};
    Bcp47LanguageTag       language;
    TextShapingFeatures    features;

    [[nodiscard]] bool operator==(const EffectiveTextState& o) const noexcept {
        return text      == o.text
            && font      == o.font
            && direction == o.direction
            && language  == o.language
            && features  == o.features;
    }
    [[nodiscard]] bool operator!=(const EffectiveTextState& o) const noexcept {
        return !(*this == o);
    }
};

// ──────────────────────────────────────────────────────────────────────────
// update_text_run_shape_per_frame — cheap path (no re-shaping)
// ──────────────────────────────────────────────────────────────────────────

/// Re-evaluate `shape->animators` at `time`, writing per-glyph state back
/// into `shape->glyphs`.
///
/// No-op when:
///   - `shape.layout == nullptr`
///   - `shape.animators.empty()`
///   - `time` matches what was last evaluated (caller may short-circuit,
///     but the function itself re-runs deterministically)
///
/// Cost: O(num_glyphs × num_animator_specs).  Does NOT re-shape with
/// HarfBuzz, so even large animator stacks (< 1000 glyphs × ~10 specs)
/// complete in well under a millisecond on modern hardware.
///
/// The mutated `glyphs` vector feeds the existing `hash_text_run_shape`
/// cache key in `TextRunNode::cache_key()` — when glyph state changes
/// across frames the cache key invalidates the stale entry automatically.
/// @param cache — TextLayoutCache* for layout reuse.  If nullptr, the
/// function uses a local throwaway cache (layout is NOT shared across
/// frames — every call re-shapes).  Production paths MUST supply a
/// pointer to the engine/session-owned cache.
void update_text_run_shape_per_frame(TextRunShape& shape, SampleTime time,
                                     TextLayoutCache* cache = nullptr);

// ──────────────────────────────────────────────────────────────────────────
// apply_active_state_to_text_run_shape — AnimatedTextDocument bridge
// ──────────────────────────────────────────────────────────────────────────

/// Apply an `AnimatedTextDocument` sample state to a TextRunShape.
///
/// Behaviour by transition type:
///   * Hold / Cut — render the `state.active->utf8`.  `transition_text`
///     is empty for these, so the active document is used as-is.  No
///     crossfade slots touched.
///   * Scramble / Morph — render `state.transition_text` if non-empty
///     (filled by `AnimatedTextDocument::sample_at` for these modes);
///     fall back to `state.active->utf8` otherwise.  No crossfade
///     slots touched (Scramble/Morph shape their per-frame text into
///     `shape.layout` directly).
///   * DissolveLayouts (PR 11) — render the `state.active->utf8` into
///     `shape.layout` AND populate `shape.dissolve_layout` from
///     `state.dissolve_from` (and `shape.dissolve_glyphs` seeded
///     from the new placed run) when `state.mix` is strictly inside
///     (0, 1).  Outside the gap (mix on the boundary, dissolve_from
///     null, transition != DissolveLayouts), the crossfade slots are
///     reset so the compositor short-circuits.  The blend itself
///     happens in the compositor (`draw_text_run`'s tiered loop),
///     which composites the outgoing glyph vector with per-glyph
///     opacity fold `(1 - shape.dissolve_mix)` against the active
///     side.  See `compute_text_run_world_bbox` for the bounding-box
///     union and `draw_text_run` for the per-glyph loop.
///
/// Algorithm (M1.5#2 decomposition):
///   1. text_state_sampler::select_target_text(state)        → target_text
///   2. text_state_sampler::is_in_dissolve_gap(state)       → in_gap
///   3. text_font_state::compute_effective_font(state, layout) → FontSpec
///   4. text_layout_rebuild::target_effective_state(...)
///                                       → EffectiveTextState (target)
///   5. text_layout_rebuild::current_effective_state(layout) (if any)
///                                       → EffectiveTextState (current)
///   6. fast-path: if both states match, return false (no rebuild).
///   7. otherwise text_layout_rebuild::rebuild_active_side + (if in_gap)
///      rebuild_crossfade_slot, then swap shape->layout,
///      re-seed glyphs, and update crossfade_* slots per PR 11 lifecycle.
///
/// @return true iff active-side layout was replaced.
/// @param cache — TextLayoutCache* for layout reuse.  Must be non-null
/// in production; nullptr in tests uses a local throwaway cache.
[[nodiscard]] bool apply_active_state_to_text_run_shape(
    TextRunShape& shape,
    const ActiveTextState& state,
    FontEngine& engine,
    const TextLayoutSpec& layout_spec,
    TextLayoutCache* cache = nullptr
);

// ──────────────────────────────────────────────────────────────────────────
// prewarm_text_run_layout_for_frame — PR 10 pre-warm hook
// ──────────────────────────────────────────────────────────────────────────

/// Eagerly populate `shared_text_layout_cache()` with the layout that
/// `shape` will produce at `frame` if its attached AnimatedTextDocument
/// follows a Scramble / Morph / DissolveLayouts path.
///
/// Purpose
/// ------
/// The per-frame driver (`update_text_run_shape_per_frame`) calls
/// `apply_active_state_to_text_run_shape` on every frame.  For
/// Scramble / Morph that path runs `build_text_run` (HarfBuzz shaping)
/// because the transition_text differs every frame.  Without a
/// prewarm hook, the first frame of a transition always misses the
/// layout cache and pays the full shaping cost.
///
/// When called just before frame `frame`, prewarm puts the upcoming
/// transition_text's layout into the shared cache so the running
/// frame's `apply_active_state_to_text_run_shape` finds it on the
/// first lookup.  The hook is idempotent: storing the same key twice
/// is a no-op (or, depending on LRU, a fresh LRU promotion).
///
/// Implementation (M1.5#2): delegates to text_state_sampler +
/// text_font_state + text_layout_rebuild::prewarm_for_frame, symmetric
/// with `apply_active_state_to_text_run_shape` so the cache key we
/// build lines up with the key the driver will hit.
///
/// Pre-conditions
/// --------------
///   * `shape.animated_doc` must be non-null — otherwise no-op.
///   * `shape.engine`      must be non-null — otherwise no-op.
///   * `frame` must be a valid integral frame.
///
/// Returns `true` when a new layout was inserted into the shared
/// cache.  Returns `false` for no-op (static shape, no engine,
/// empty target_text, or layout already cached).
///
/// @param cache — TextLayoutCache* for layout reuse.  Must be non-null
/// in production; nullptr in tests uses a local throwaway cache.
[[nodiscard]] bool prewarm_text_run_layout_for_frame(
    const TextRunShape& shape,
    Frame frame,
    TextLayoutCache* cache = nullptr
);

} // namespace chronon3d
