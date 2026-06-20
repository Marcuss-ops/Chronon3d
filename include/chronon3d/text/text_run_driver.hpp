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

} // namespace chronon3d
