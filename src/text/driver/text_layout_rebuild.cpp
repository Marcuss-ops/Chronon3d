// SPDX-License-Identifier: MIT
//
// M1.5#2 — implementation for text_layout_rebuild.  See header.

#include "text_layout_rebuild.hpp"

#include "text_font_state.hpp"      // compute_effective_font_for_prewarm
#include "text_state_sampler.hpp"   // is_in_dissolve_gap / select_target_text

namespace chronon3d::text::driver {

EffectiveTextState current_effective_state(const TextRunLayout& layout) {
    EffectiveTextState s;
    s.text       = layout.source_text;
    s.font       = font_layout_identity_of(layout);
    s.direction  = layout.direction;
    s.language   = layout.language;
    s.features   = layout.features;
    return s;
}

EffectiveTextState target_effective_state(
    const std::string& target_text,
    const FontSpec& effective_font,
    const TextLayoutSpec& /*layout_spec*/  // M1.5#2 contract: layout_spec does
                                           // not yet carry per-frame direction
                                           // / language / features overrides.
                                           // They default below; future
                                           // shape-side fields route here.
) {
    EffectiveTextState s;
    s.text       = target_text;
    s.font       = font_layout_identity_of(
        effective_font,
        effective_font.font_size,
        /*shaping_features=*/""      // layout_spec has no .shaping.features yet
    );
    // direction / language / features default-constructed:
    //   direction = TextDirection::Auto, language = "", features = ""
    // Mirrors the cache-key feeding in build_text_run for fields that
    // the CURRENT TextLayoutSpec cannot override.  Future shape-level
    // fields (per TICKET-103a colligation) will thread through here.
    return s;
}

bool fast_path_hit(
    const EffectiveTextState& current,
    const EffectiveTextState& target
) noexcept {
    return current == target;  // operator==(EffectiveTextState)
}

bool rebuild_active_side(
    TextRunShape& shape,
    const std::string& target_text,
    const FontSpec& effective_font,
    const TextLayoutSpec& layout_spec,
    FontEngine& engine,
    TextLayoutCache* cache
) {
    if (target_text.empty()) {
        return false;
    }
    TextDocument td;
    td.utf8         = target_text;
    td.defaults.font = effective_font;  // P0-2: project pre-computed effective font
    td.split_paragraphs();

    auto* cache_ptr = (cache != nullptr) ? cache : shape.cache;
    TextRunBuildResult result =
        build_text_run(td, engine, layout_spec, cache_ptr);

    if (result.paragraphs.empty() || !result.paragraphs.front()) {
        return false;
    }
    shape.layout = result.paragraphs.front();
    shape.glyphs = make_initial_glyph_states(shape.layout->placed);
    return true;
}

bool rebuild_dissolve_slot(
    TextRunShape& shape,
    const ActiveTextState& state,
    const FontSpec& effective_font_outgoing,
    const TextLayoutSpec& layout_spec,
    FontEngine& engine,
    TextLayoutCache* cache
) {
    if (state.dissolve_from == nullptr) {
        return false;
    }
    const std::string& cf_utf8 = state.dissolve_from->utf8;

    const bool no_gap_work =
        !is_in_dissolve_gap(state) || cf_utf8.empty();

    if (no_gap_work) {
        // Out-of-gap clear (PR 11 contract)
        if (shape.dissolve_layout) {
            shape.dissolve_layout.reset();
        }
        if (!shape.dissolve_glyphs.empty()) {
            shape.dissolve_glyphs.clear();
        }
        shape.dissolve_mix = 0.0f;
        return false;
    }

    // Inside the gap: fast-path skip when source_text already matches.
    const bool same_layout =
        shape.dissolve_layout
        && shape.dissolve_layout->source_text == cf_utf8;

    if (same_layout && shape.dissolve_mix == state.mix) {
        // mix already tracked; cache lookup would hit anyway.
        shape.dissolve_mix = state.mix;  // idempotent refresh
        return false;
    }

    if (cf_utf8.empty()) {
        shape.dissolve_layout.reset();
        shape.dissolve_glyphs.clear();
        shape.dissolve_mix = 0.0f;
        return false;
    }

    TextDocument cf_td;
    cf_td.utf8         = cf_utf8;
    cf_td.defaults.font = effective_font_outgoing;
    cf_td.split_paragraphs();

    auto* cache_ptr = (cache != nullptr) ? cache : shape.cache;
    TextRunBuildResult cf_result =
        build_text_run(cf_td, engine, layout_spec, cache_ptr);

    if (!cf_result.paragraphs.empty() && cf_result.paragraphs.front()) {
        shape.dissolve_layout = cf_result.paragraphs.front();
        shape.dissolve_glyphs = make_initial_glyph_states(
            shape.dissolve_layout->placed);
        shape.dissolve_mix = state.mix;
        return true;
    }
    // Build failed (e.g. font load failure) — clear slot rather than
    // leave stale state.
    shape.dissolve_layout.reset();
    shape.dissolve_glyphs.clear();
    shape.dissolve_mix = 0.0f;
    return false;
}

bool prewarm_for_frame(
    const TextRunShape& shape,
    const ActiveTextState& state,
    Frame /*frame_arg*/,  // currently unused (sample_at carries the time)
    const TextLayoutSpec& layout_spec,
    FontEngine& engine,
    TextLayoutCache* cache
) {
    if (state.active == nullptr) {
        return false;
    }
    const std::string target_text = select_target_text(state);
    if (target_text.empty()) {
        return false;
    }

    // Active-side prewarm
    FontSpec effective_font = compute_effective_font_for_prewarm(state, shape.layout.get());
    TextDocument td;
    td.utf8         = target_text;
    td.defaults.font = effective_font;
    td.split_paragraphs();

    auto* cache_ptr = (cache != nullptr) ? cache : shape.cache;
    TextRunBuildResult result =
        build_text_run(td, engine, layout_spec, cache_ptr);

    const bool active_ok = !result.paragraphs.empty();

    // Crossfade-side prewarm (only inside the gap)
    bool cf_ok = true;
    const bool in_gap = is_in_dissolve_gap(state);
    if (in_gap && state.dissolve_from != nullptr
        && !state.dissolve_from->utf8.empty())
    {
        TextDocument cf_td;
        cf_td.utf8         = state.dissolve_from->utf8;
        cf_td.defaults.font = state.dissolve_from->defaults.font;
        cf_td.split_paragraphs();
        TextRunBuildResult cf_result =
            build_text_run(cf_td, engine, layout_spec, cache_ptr);
        cf_ok = !cf_result.paragraphs.empty();
    }

    return active_ok && cf_ok;
}

}  // namespace chronon3d::text::driver
