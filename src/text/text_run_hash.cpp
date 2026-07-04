// SPDX-License-Identifier: MIT
//
// src/text/text_run_hash.cpp — M1.5#4 commitment 1 of 3.
//
// Single-responsibility extraction of the `hash_text_run_shape` family
// from the historical `text_run.cpp` monolith.  Body content is a
// VERBATIM move from the historical text_run.cpp; no behavioural
// mutations, no signature changes, no new code paths.  Future M1.5#5+
// commits may refactor the bodies to read FROM the canonical
// FontLayoutIdentity (M1.5#4 final lock — Commit D), but for this
// commit the legacy field-by-field fold is preserved.
//
// Dependency rationale:
//   - text_run_hash.hpp: forward-declared free functions (matches the
//                         M1.5#3 umbrella sub-header split).
//   - text_run.hpp: provides TextRunShape complete type + the
//                   Frame-overload returns-base hash_text_run_shape(s)
//                   transitively.
//   - animated_text_document.hpp: ActiveTextState + sample_at()
//                                   (PR 10 doc fold).
//   - text_animator_property.hpp: GlyphInstanceState complete type
//                                  (used in the per-glyph fold loop).
//   - render_graph_hashing.hpp:   hash_combine / hash_value /
//                                 hash_color / hash_vec2 / hash_string /
//                                 hash_bytes.

#include <chronon3d/text/text_run_hash.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

// PR 10 — sample-time fold of AnimatedTextDocument for cache-key
// invalidation.  We fold a small subset of ActiveTextState into the hash
// so Scramble / Morph / CrossfadeLayouts frames don't share stale
// entries.  No behavioural mutations: `hash_text_run_shape` remains pure
// (no reshaping, no cache writes), only mirrors what the per-frame
// driver will SEE when it executes the frame.
#include <chronon3d/text/animated_text_document.hpp>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextRunShape hashing
// ═══════════════════════════════════════════════════════════════════════════

u64 hash_text_run_shape(const TextRunShape& s) {
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;
    using chronon3d::graph::hash_color;
    using chronon3d::graph::hash_vec2;

    // Layout hash (text + font + shaping)
    u64 seed = s.layout ? s.layout->layout_hash() : u64(0);

    // Animator hash (per-glyph state)
    for (const auto& g : s.glyphs) {
        seed = hash_combine(seed, hash_value(g.glyph_id));
        seed = hash_combine(seed, hash_value(g.position.x));
        seed = hash_combine(seed, hash_value(g.position.y));
        seed = hash_combine(seed, hash_value(g.position.z));
        seed = hash_combine(seed, hash_value(g.scale.x));
        seed = hash_combine(seed, hash_value(g.scale.y));
        seed = hash_combine(seed, hash_value(g.scale.z));
        seed = hash_combine(seed, hash_value(g.rotation.x));
        seed = hash_combine(seed, hash_value(g.rotation.y));
        seed = hash_combine(seed, hash_value(g.rotation.z));
        seed = hash_combine(seed, hash_value(g.opacity));
        seed = hash_combine(seed, hash_value(g.blur));
        seed = hash_combine(seed, hash_color(g.fill));
        seed = hash_combine(seed, hash_color(g.stroke));
        seed = hash_combine(seed, hash_value(g.stroke_width));
    }

    // Material hash (premium material settings)
    seed = hash_combine(seed, hash_value(s.material.enabled));
    if (s.material.enabled) {
        seed = hash_combine(seed, hash_color(s.material.top_color));
        seed = hash_combine(seed, hash_color(s.material.bottom_color));
        seed = hash_combine(seed, hash_value(s.material.bevel_px));
        seed = hash_combine(seed, hash_value(s.material.emissive));
        seed = hash_combine(seed, hash_value(s.material.use_material_glow));
        seed = hash_combine(seed, hash_value(s.material.glow_radius));
        seed = hash_combine(seed, hash_value(s.material.glow_intensity));
        seed = hash_combine(seed, hash_value(s.material.use_material_shadow));
        seed = hash_combine(seed, hash_value(s.material.shadow_blur));
        seed = hash_combine(seed, hash_value(s.material.shadow_opacity));
    }

    // Paint
    seed = hash_combine(seed, hash_color(s.paint.fill));
    seed = hash_combine(seed, hash_value(s.paint.stroke_enabled));
    seed = hash_combine(seed, hash_color(s.paint.stroke_color));
    seed = hash_combine(seed, hash_value(s.paint.stroke_width));

    // Shadows
    for (const auto& shadow : s.shadows) {
        seed = hash_combine(seed, hash_value(shadow.enabled));
        seed = hash_combine(seed, hash_vec2(shadow.offset));
        seed = hash_combine(seed, hash_value(shadow.blur));
        seed = hash_combine(seed, hash_value(shadow.opacity));
        seed = hash_combine(seed, hash_color(shadow.color));
    }

    return seed;
}

// ═════════════════════════════════════════════════════════════════════════
// hash_text_run_shape (frame overload) — PR 10
// ═════════════════════════════════════════════════════════════════════════
//
// Mirrors the per-frame driver's view of the shape.  When
// `shape.animated_doc` is bound, this overload samples the doc at `frame`
// and folds a subset of ActiveTextState into the hash so Scramble /
// Morph / CrossfadeLayouts frames invalidate the executor's per-frame
// node cache without false hits.
//
// Fold matrix (PR 10 + PR 11):
//   - transition_type (Y)         — drives the renderer branch.
//   - active->utf8_bytes (Y)      — Hold / Cut / CrossfadeLayouts have
//                                    empty transition_text; we need the
//                                    underlying active document to differ.
//   - active->defaults.font (Y)   — a font-only Cut (text unchanged,
//                                    weight swapped) must invalidate.
//                                    Captures the intent of the next
//                                    apply_active_state_to_text_run_shape.
//   - crossfade_from->utf8 + font (Y, PR 11)
//                                  — CrossfadeLayouts renders BOTH
//                                    active and crossfade_from per-
//                                    glyph in the compositor (PR 11
//                                    implementation).  A change in
//                                    outgoing text or outgoing font
//                                    must invalidate the cache key.
//   - transition_text_bytes (Y)    — random per-frame scramble bytes
//                                    (folded only when non-empty; XXH3
//                                    already discriminates empty vs non-
//                                    empty so we skip the redundant
//                                    length-prefix fold).
//   - morph_map_bytes (Y)          — Morph topology (pair<i32,i32>).
//   - mix_value (Y)                — continuous visual params
//                                    (CrossfadeLayouts fold included).
//   - length-prefix (N)            — XXH3 already absorbs sequence length.
//
// Cost: O(n_keyframes) binary search inside `sample_at` +
// variable-length scramble/morph generation.  In practice keyframe
// lists are tiny (<20) and scramble/morph expansion costs are sub-µs
// for short strings; the folder itself is bounded by a single call per
// node per frame at cache_key() time.

u64 hash_text_run_shape(const TextRunShape& s, Frame frame) {
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_bytes;

    u64 seed = hash_text_run_shape(s);  // base fold (layout + glyphs + paint + material + shadows)

    // ── PR 10 — fold the AnimatedTextDocument state at `frame` ──────
    //
    // Only meaningful when the shape carries an animated_doc.  Static
    // shapes fall through (no doc fold → the new overload reduces to
    // the legacy one, so non-animated text gets the same hash it
    // always did — safe for downstream consumers).
    if (!s.animated_doc) {
        return seed;
    }

    const ActiveTextState state = s.animated_doc->sample_at(frame);

    // Fold transition_type first so different transition modes can
    // never collide on the same downstream bytes (Scramble vs Morph
    // with identical-looking bytes is detectable only here).
    seed = hash_combine(seed, hash_value(static_cast<u8>(state.transition)));

    // Fold the active document's utf8 + font.  Both cover Hold / Cut /
    // CrossfadeLayouts (where transition_text is empty) AND serve as
    // belt-and-braces discriminators for the scramble/morph cases
    // (where the LAYOUT cache key itself is keyed on
    // layout->source_text once the per-frame driver applies the new
    // active state).  The font fold catches font-swap keyframes that
    // would otherwise produce identical hashes despite a re-shape.
    if (state.active != nullptr) {
        const auto& d = state.active->defaults;
        seed = hash_combine(seed, hash_string(state.active->utf8));
        seed = hash_combine(seed, hash_string(d.font.font_path));
        seed = hash_combine(seed, hash_string(d.font.font_family));
        seed = hash_combine(seed, hash_value(d.font.font_weight));
        seed = hash_combine(seed, hash_string(d.font.font_style));
        seed = hash_combine(seed, hash_value(d.font.font_size));
    }

    // Fold the crossfade_from side conditional on whether
    // sample_at returned a non-null outgoing document.  PR 11:
    // only CrossfadeLayouts sets this pointer, but we fold
    // unconditionally when set so any future transition mode that
    // populates crossfade_from is correctly covered by the cache
    // key.  Mirrors the active>defaults.font fold field-for-field
    // so a font-only delta on the outgoing side (the path that
    // could otherwise produce a stale shape hash across the gap)
    // invalidates the cache.
    if (state.crossfade_from != nullptr) {
        const auto& d = state.crossfade_from->defaults;
        seed = hash_combine(seed, hash_string(state.crossfade_from->utf8));
        seed = hash_combine(seed, hash_string(d.font.font_path));
        seed = hash_combine(seed, hash_string(d.font.font_family));
        seed = hash_combine(seed, hash_value(d.font.font_weight));
        seed = hash_combine(seed, hash_string(d.font.font_style));
        seed = hash_combine(seed, hash_value(d.font.font_size));
    }

    // Fold transition_text bytes for Scramble / Morph (only when
    // non-empty; XXH3 differs deterministically between empty and any
    // non-empty input so an explicit length prefix is redundant).
    if (!state.transition_text.empty()) {
        seed = hash_combine(
            seed,
            hash_bytes(state.transition_text.data(),
                       state.transition_text.size()));
    }

    // Fold morph_map topology for Morph.  Other transitions leave
    // morph_map empty so the size-fold collapses to a single hash
    // value, but it's intentionally retained (not redundant) because
    // it strongly discriminates between Morph (size > 0) and
    // Hold/Cut/Scramble/CrossfadeLayouts (size == 0).  Kept as a
    // structural classifier independent of `mix` so the two signals
    // can be reasoned about separately.
    seed = hash_combine(
        seed,
        hash_value(static_cast<u64>(state.morph_map.size())));
    for (const auto& pair : state.morph_map) {
        seed = hash_combine(seed, hash_value(static_cast<i64>(pair.first)));
        seed = hash_combine(seed, hash_value(static_cast<i64>(pair.second)));
    }

    // Fold mix as a float — drives continuous visual params even when
    // transition_text is identical (e.g. two consecutive Scramble frames
    // with the same content but different mix values).
    seed = hash_combine(seed, hash_value(state.mix));

    return seed;
}

} // namespace chronon3d
