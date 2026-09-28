// SPDX-License-Identifier: MIT
//
// M1.5#2 — implementation for text_layout_rebuild.  See header.

#include "text_layout_rebuild.hpp"

#include "text_font_state.hpp"      // compute_effective_font_for_prewarm
#include "text_state_sampler.hpp"   // is_in_dissolve_gap / select_target_text

#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

namespace chronon3d::text::driver {

namespace {

/// Build (or pull from cache) the layout that composes EVERY paragraph of
/// `doc`.
///
/// This replaces the historical `build_text_run(doc, engine, spec, cache)`
/// + `result.paragraphs.front()` pair.  `build_text_run` returns one
/// `TextRunLayout` per `\n`-separated paragraph, and the front() of that
/// vector was the only entry ever rendered — a multi-line phrase came out
/// as its first line only.  `compile_text_document_layout` compiles the
/// same paragraphs with the same request fields (primary font from
/// `doc.defaults.font`, `features` from the layout spec, Auto direction /
/// empty language) and returns them already stacked into a single layout.
[[nodiscard]] std::shared_ptr<TextRunLayout> build_merged_layout(
    const TextDocument& doc,
    const TextLayoutSpec& layout_spec,
    FontEngine& engine,
    TextLayoutCache* cache
) {
    TextLayoutRequest request{&doc, &layout_spec, doc.defaults.font, 0};
    request.features = layout_spec.features;
    TextCompileServices services{&engine, cache, {}};
    auto compiled = compile_text_document_layout(request, services);
    if (!compiled) {
        spdlog::warn(
            "build_merged_layout: compile_text_document_layout failed — {}",
            compiled.error().message);
        return nullptr;
    }
    return compiled.value();
}

} // namespace

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

    auto merged = build_merged_layout(
        td, layout_spec, engine, (cache != nullptr) ? cache : shape.cache);
    if (!merged) {
        return false;
    }
    shape.layout = std::move(merged);
    reset_glyph_states_in_place(shape.glyphs, shape.layout->placed);
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
        // A transition can leave the dissolve interval without retaining an
        // outgoing keyframe. Clear the previous slot explicitly so a reused
        // shape cannot carry stale crossfade pixels into a Hold state.
        shape.dissolve_layout.reset();
        shape.dissolve_glyphs.clear();
        shape.dissolve_mix = 0.0f;
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

    auto cf_merged = build_merged_layout(
        cf_td, layout_spec, engine, (cache != nullptr) ? cache : shape.cache);
    if (cf_merged) {
        shape.dissolve_layout = std::move(cf_merged);
        reset_glyph_states_in_place(shape.dissolve_glyphs, shape.dissolve_layout->placed);
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
    TextLayoutRequest active_request{&td, &layout_spec, td.defaults.font, 0};
    active_request.features = layout_spec.features;
    TextCompileServices active_services{&engine, cache_ptr, {}};
    const bool active_ok = static_cast<bool>(
        compile_text_document_layout(active_request, active_services));

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
        TextLayoutRequest cf_request{&cf_td, &layout_spec, cf_td.defaults.font, 0};
        cf_request.features = layout_spec.features;
        TextCompileServices cf_services{&engine, cache_ptr, {}};
        cf_ok = static_cast<bool>(
            compile_text_document_layout(cf_request, cf_services));
    }

    return active_ok && cf_ok;
}

}  // namespace chronon3d::text::driver
