// SPDX-License-Identifier: MIT
//
// M1.5#2 — internal helper: text_layout_rebuild.
// Owns the per-frame rebuild path: EffectiveTextState projection,
// fast-path comparison, `build_text_run` invocation with cache,
// DissolveLayouts lifecycle on `shape.crossfade_*`, and the prewarm
// hook.  All mutations to `shape->layout` / `shape->glyphs` /
// `shape->crossfade_*` route through this module so the rules are
// co-located and testable.
//
// Internal-only — NOT in include/chronon3d/.

#pragma once

#include <chronon3d/text/animated_text_document.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_run_builder.hpp>           // build_text_run
#include <chronon3d/text/text_run_driver.hpp>           // EffectiveTextState (M1.5#2)
#include <chronon3d/scene/builders/builder_params.hpp>

namespace chronon3d::text::driver {

/// Project the CURRENT layout (post-swap state) into an
/// `EffectiveTextState` snapshot.  This is what the fast-path compares
/// against the next-frame target.  Pre-M1.5#2 only source_text +
/// FontLayoutIdentity was compared (P0-2 fix); M1.5#2 adds direction +
/// language + features so a bidi-flip or language-flip correctly
/// invalidates the cache.
[[nodiscard]] EffectiveTextState current_effective_state(
    const TextRunLayout& layout
);

/// Project the NEXT-frame `ActiveTextState` + target_text + layout_spec
/// into an `EffectiveTextState` snapshot for fast-path comparison.
/// Identical fields compared field-by-field against `current_effective_state`.
[[nodiscard]] EffectiveTextState target_effective_state(
    const std::string& target_text,
    const FontSpec& effective_font,
    const TextLayoutSpec& layout_spec
);

/// True iff `current == target`.  Convenience wrapper — the operator==
/// lives on the struct, but routing through here keeps the slow-path /
/// fast-path control flow explicit in the orchestrator and gives us a
/// single seam to instrument (e.g. profile counters).
[[nodiscard]] bool fast_path_hit(
    const EffectiveTextState& current,
    const EffectiveTextState& target
) noexcept;

/// Active-side rebuild: build a single-paragraph `TextDocument` from
/// `target_text` + `effective_font`, run `build_text_run` with `cache`,
/// and — on success — swap `shape.layout` + re-seed `shape.glyphs`.
/// Returns true iff the rebuild produced a layout (caller signals
/// upstream cache invalidation when true).
[[nodiscard]] bool rebuild_active_side(
    TextRunShape& shape,
    const std::string& target_text,
    const FontSpec& effective_font,
    const TextLayoutSpec& layout_spec,
    FontEngine& engine,
    TextLayoutCache* cache
);

/// DissolveLayouts lifecycle on `shape.crossfade_*` slots (PR 11):
///   - inside the gap   → rebuild `dissolve_layout`+`dissolve_glyphs`
///                       with the existing fast-path skip on cache hit;
///   - outside the gap  → reset dissolve_layout, clear dissolve_glyphs,
///                       zero dissolve_mix.
///
/// `in_gap` is supplied by the orchestrator (via
/// `text_state_sampler::is_in_dissolve_gap`).
[[nodiscard]] bool rebuild_dissolve_slot(
    TextRunShape& shape,
    const ActiveTextState& state,
    const FontSpec& effective_font_outgoing,  // state.dissolve_from->defaults.font
    const TextLayoutSpec& layout_spec,
    FontEngine& engine,
    TextLayoutCache* cache
);

/// Prewarm path: shapes both the active-side and outgoing-slot layouts
/// (when in the gap) into `cache`, then returns true iff both lookups
/// succeeded.  Mirrors `apply_active_state_to_text_run_shape` exactly so
/// the cache key we write lines up with the key the driver will hit.
[[nodiscard]] bool prewarm_for_frame(
    const TextRunShape& shape,
    const ActiveTextState& state,
    Frame frame,
    const TextLayoutSpec& layout_spec,
    FontEngine& engine,
    TextLayoutCache* cache
);

}  // namespace chronon3d::text::driver
