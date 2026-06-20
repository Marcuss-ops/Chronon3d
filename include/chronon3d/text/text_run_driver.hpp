#pragma once

// ═══════════════════════════════════════════════════════════════════════════
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
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run_builder.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>

namespace chronon3d {

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
void update_text_run_shape_per_frame(TextRunShape& shape, SampleTime time);

// ──────────────────────────────────────────────────────────────────────────
// apply_active_state_to_text_run_shape — AnimatedTextDocument bridge
// ──────────────────────────────────────────────────────────────────────────

/// Apply an `AnimatedTextDocument` sample state to a TextRunShape.
///
/// Behaviour by transition type:
///   * Hold / Cut / CrossfadeLayouts — render the `state.active->utf8`.
///     `state.transition_text` is empty for these, so the active
///     document is used as-is.  CrossfadeLayouts blends the per-glyph
///     state in the renderer layer (NOT here — PR 8 renders the active
///     side cleanly rather than maintaining two parallel layouts).
///   * Scramble / Morph — render `state.transition_text` if non-empty
///     (filled by `AnimatedTextDocument::sample_at` for these modes);
///     fall back to `state.active->utf8` otherwise.
///
/// Algorithm:
///   1. Decide `target_text` from `state.transition` + `state.transition_text`.
///   2. Fast-path: if `shape.layout->source_text == target_text` AND the
///      layout's font weight/path/style match the active document's
///      defaults font, skip rebuild (no-op return).
///   3. Build a single-paragraph `TextDocument` from `target_text` with
///      font/style fields copied from the current `shape.layout`.
///   4. Call `build_text_run` with the shared `TextLayoutCache` so
///      consecutive frames with the same target text hit cache.
///   5. Swap the parsed `TextRunLayout` into `shape.layout` (preserving
///      paint/material/shadows/animators).  Re-seed `shape.glyphs`
///      from the new placed run via `make_initial_glyph_states`.
///   6. Return true (layout changed) so the caller invalidates any
///      layout-resident cache downstream.
///
/// @return true iff `shape->layout` was replaced (caller may want to
///         invalidate downstream caches); false if a no-op.
[[nodiscard]] bool apply_active_state_to_text_run_shape(
    TextRunShape& shape,
    const ActiveTextState& state,
    FontEngine& engine,
    const TextLayoutSpec& layout_spec
);

// ──────────────────────────────────────────────────────────────────────────
// prewarm_text_run_layout_for_frame — PR 10 pre-warm hook
// ──────────────────────────────────────────────────────────────────────────

/// Eagerly populate `shared_text_layout_cache()` with the layout that
/// `shape` will produce at `frame` if its attached AnimatedTextDocument
/// follows a Scramble / Morph / CrossfadeLayouts path.
///
/// Purpose
/// -------
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
/// Pre-conditions
/// ---------------
///   * `shape.animated_doc` must be non-null — otherwise no-op.
///   * `shape.engine`      must be non-null — otherwise no-op (the
///                          confirm-skip is logged once at debug
///                          level so production telemetry stays quiet).
///   * `frame` must be a valid integral frame.
///
/// Returns `true` when a new layout was inserted into the shared
/// cache.  Returns `false` for no-op (static shape, no engine,
/// empty target_text, or layout already cached).
///
/// Caller responsibility: the hook populates the cache but does NOT
/// invalidate any executor-level per-frame node cache.  When the
/// executor runs at `frame`, the new transition_text produces a
/// different `hash_text_run_shape` (the PR 10 sample-time overload
/// folds transition_text + morph_map), so the NodeCache layer
/// naturally invalidates via the new key.
///
/// Performance
/// -----------
/// Cost is bounded by HarfBuzz shaping + paragraph composition for
/// one TextDocument (the worst case is the first call on a fresh
/// scene).  After this call, every subsequent frame with the same
/// transition_text shares the cached layout — the hot path becomes
/// O(1) cache lookup.
///
/// Thread-safety: the helper writes to the shared_text_layout_cache()
/// singleton, which is internally thread-safe.  Concurrent callers
/// may race but the cache is idempotent on the same key.
///
/// TODO(PR11+): add a CI font fixture so the success path can be
/// exercised by tests.  The current PR 10 tests cover only the no-op
/// guard rails (no animated_doc, no engine) because the cache-write
/// path requires HarfBuzz to actually run.  A DejaVu Sans fixture in
/// `tests/fixtures/` plus a `test_prewarm_text_layout_cache.cpp`
/// would close this gap.
[[nodiscard]] bool prewarm_text_run_layout_for_frame(
    const TextRunShape& shape,
    Frame frame
);

} // namespace chronon3d
