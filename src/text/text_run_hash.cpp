// ═══════════════════════════════════════════════════════════════════════════
// src/text/text_run_hash.cpp — M1.5#4
//
//   Extracted from src/text/text_run.cpp.  Contains:
//     - TextRunLayout::layout_hash()
//     - TextRunLayout::shaping_hash()
//     - hash_text_run_shape(TextRunShape)
//     - hash_text_run_shape(TextRunShape, Frame)
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

// PR 10 — sample-time fold of AnimatedTextDocument for cache-key
// invalidation.  We fold a small subset of ActiveTextState into the hash
// so Scramble / Morph / CrossfadeLayouts frames don't share stale
// entries.
#include <chronon3d/text/animated_text_document.hpp>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextRunLayout hashing
// ═══════════════════════════════════════════════════════════════════════════

u64 TextRunLayout::layout_hash() const {
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;

    u64 seed = hash_string(source_text);
    seed = hash_combine(seed, hash_string(font.font_path));
    seed = hash_combine(seed, hash_string(font.font_family));   // P0-2
    seed = hash_combine(seed, hash_value(font.font_weight));
    seed = hash_combine(seed, hash_string(font.font_style));
    seed = hash_combine(seed, hash_value(font_size));
    seed = hash_combine(seed, hash_value(tracking));
    seed = hash_combine(seed, hash_value(static_cast<int>(wrap)));
    seed = hash_combine(seed, hash_value(static_cast<int>(direction)));
    seed = hash_combine(seed, hash_string(language));
    // TICKET-103a — OpenType shaping features are part of the layout
    // identity (e.g. "kern=1" vs "kern=0" produce different glyph
    // advances on ligature-heavy runs).
    seed = hash_combine(seed, hash_string(features));
    return seed;
}

u64 TextRunLayout::shaping_hash() const {
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;

    u64 seed = hash_string(font.font_path);
    seed = hash_combine(seed, hash_string(font.font_family));   // P0-2
    seed = hash_combine(seed, hash_value(font.font_weight));
    seed = hash_combine(seed, hash_string(font.font_style));
    seed = hash_combine(seed, hash_value(font_size));
    seed = hash_combine(seed, hash_value(tracking));
    seed = hash_combine(seed, hash_value(static_cast<int>(wrap)));
    seed = hash_combine(seed, hash_value(static_cast<int>(direction)));
    seed = hash_combine(seed, hash_string(language));
    // TICKET-103a — OT shaping features fold into shaping_hash so
    // compile-time cache partitioning discriminates on feature string.
    seed = hash_combine(seed, hash_string(features));
    return seed;
}

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

u64 hash_text_run_shape(const TextRunShape& s, Frame frame) {
    using chronon3d::graph::hash_combine;
    using chronon3d::graph::hash_value;
    using chronon3d::graph::hash_string;
    using chronon3d::graph::hash_bytes;

    u64 seed = hash_text_run_shape(s);

    if (!s.animated_doc) {
        return seed;
    }

    const ActiveTextState state = s.animated_doc->sample_at(frame);

    seed = hash_combine(seed, hash_value(static_cast<u8>(state.transition)));

    if (state.active != nullptr) {
        const auto& d = state.active->defaults;
        seed = hash_combine(seed, hash_string(state.active->utf8));
        seed = hash_combine(seed, hash_string(d.font.font_path));
        seed = hash_combine(seed, hash_string(d.font.font_family));
        seed = hash_combine(seed, hash_value(d.font.font_weight));
        seed = hash_combine(seed, hash_string(d.font.font_style));
        seed = hash_combine(seed, hash_value(d.font.font_size));
    }

    if (state.crossfade_from != nullptr) {
        const auto& d = state.crossfade_from->defaults;
        seed = hash_combine(seed, hash_string(state.crossfade_from->utf8));
        seed = hash_combine(seed, hash_string(d.font.font_path));
        seed = hash_combine(seed, hash_string(d.font.font_family));
        seed = hash_combine(seed, hash_value(d.font.font_weight));
        seed = hash_combine(seed, hash_string(d.font.font_style));
        seed = hash_combine(seed, hash_value(d.font.font_size));
    }

    if (!state.transition_text.empty()) {
        seed = hash_combine(
            seed,
            hash_bytes(state.transition_text.data(),
                       state.transition_text.size()));
    }

    seed = hash_combine(seed, hash_value(static_cast<u64>(state.morph_map.size())));
    for (const auto& pair : state.morph_map) {
        seed = hash_combine(seed, hash_value(static_cast<i64>(pair.first)));
        seed = hash_combine(seed, hash_value(static_cast<i64>(pair.second)));
    }

    seed = hash_combine(seed, hash_value(state.mix));

    return seed;
}

} // namespace chronon3d
