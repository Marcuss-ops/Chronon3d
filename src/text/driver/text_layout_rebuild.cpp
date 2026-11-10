// SPDX-License-Identifier: MIT
//
// M1.5#2 — implementation for text_layout_rebuild.  See header.

#include "text_layout_rebuild.hpp"

#include "text_font_state.hpp"      // compute_effective_font_for_prewarm
#include "text_state_sampler.hpp"   // is_in_dissolve_gap / select_target_text

#include <spdlog/spdlog.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace chronon3d::text::driver {

namespace {

[[nodiscard]] std::size_t utf8_boundary_floor(
    const std::string& text, std::size_t offset) {
    if (offset > text.size()) offset = text.size();
    while (offset > 0 && offset < text.size() &&
           (static_cast<unsigned char>(text[offset]) & 0xc0U) == 0x80U) {
        --offset;
    }
    return offset;
}

/// A transition can generate a shorter string than the destination keyframe
/// (typewriter) or a differently shaped one (morph/scramble).  Ranges are
/// authored against the keyframe text, so clip them to the visible UTF-8
/// prefix before compiling the per-frame layout.  This is a projection, not a
/// second range authority.
void clip_document_ranges(TextDocument& doc) {
    const auto clip = [&](auto& range) {
        if (range.byte_start >= doc.utf8.size()) return false;
        range.byte_start = utf8_boundary_floor(doc.utf8, range.byte_start);
        range.byte_end = utf8_boundary_floor(doc.utf8, range.byte_end);
        return range.byte_start < range.byte_end;
    };
    doc.spans.erase(
        std::remove_if(doc.spans.begin(), doc.spans.end(),
                       [&](TextStyleSpan& span) { return !clip(span); }),
        doc.spans.end());
    doc.glyph_paint_ranges.erase(
        std::remove_if(doc.glyph_paint_ranges.begin(), doc.glyph_paint_ranges.end(),
                       [&](TextGlyphPaintRange& range) { return !clip(range); }),
        doc.glyph_paint_ranges.end());
}

[[nodiscard]] TextDocument make_document_for_frame(
    const TextDocument& source, const std::string& text, const FontSpec& font) {
    TextDocument doc = source;
    doc.utf8 = text;
    doc.defaults.font = font;
    clip_document_ranges(doc);
    doc.paragraphs.clear();
    doc.split_paragraphs();
    return doc;
}

/// Apply source paint ranges to an already-shaped layout.  This mirrors the
/// static prepare path's glyph-level fill override; it intentionally does not
/// create a second span resolver.
void apply_glyph_paint_ranges(
    TextRunLayout& layout, const std::vector<TextGlyphPaintRange>& ranges) {
    if (ranges.empty()) return;
    for (auto& glyph : layout.placed.glyphs) {
        for (const auto& range : ranges) {
            if (glyph.byte_offset >= range.byte_start &&
                glyph.byte_offset < range.byte_end) {
                glyph.fill_color_override = range.color;
                break;
            }
        }
    }
}

/// Project style-span fill overrides onto the shaped glyphs.  The resolver
/// also carries them on `font_spans` for rasterization; this copy is what the
/// per-frame animator stack and `hash_text_run_shape` read, and it is the
/// projection a transition rebuild needs so a styled keyframe stays styled
/// while its text is being revealed (typewriter) or morphed.
void apply_span_fill_overrides(
    TextRunLayout& layout, const std::vector<TextStyleSpan>& spans) {
    if (spans.empty()) return;
    for (auto& glyph : layout.placed.glyphs) {
        for (const auto& span : spans) {
            if (!span.fill_color_override) continue;
            if (glyph.byte_offset >= span.byte_start &&
                glyph.byte_offset < span.byte_end) {
                glyph.fill_color_override = span.fill_color_override;
                break;
            }
        }
    }
}

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
    // Style spans are baked into the shaped glyphs (fill overrides), but
    // TextLayoutCacheKey only identifies text/font/layout.  Bypass the cache
    // whenever the document carries style ranges, otherwise a cache hit from
    // an unstyled compile silently drops the span color.
    const bool has_style_ranges =
        !doc.spans.empty() || !doc.glyph_paint_ranges.empty();
    TextLayoutRequest request{&doc, &layout_spec, doc.defaults.font, 0};
    request.features = layout_spec.features;
    TextCompileServices services{
        &engine, has_style_ranges ? nullptr : cache, {}};
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

TextDocument project_document_for_frame(
    const TextDocument& source, const std::string& text, const FontSpec& font) {
    return make_document_for_frame(source, text, font);
}

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
    const TextDocument& target_document,
    const FontSpec& effective_font,
    const TextLayoutSpec& layout_spec,
    FontEngine& engine,
    TextLayoutCache* cache
) {
    TextDocument td = make_document_for_frame(
        target_document, target_document.utf8, effective_font);

    auto merged = build_merged_layout(
        td, layout_spec, engine, (cache != nullptr) ? cache : shape.cache);
    if (!merged) {
        return false;
    }
    apply_glyph_paint_ranges(*merged, td.glyph_paint_ranges);
    apply_span_fill_overrides(*merged, td.spans);
    shape.layout = std::move(merged);
    shape.pre_shaping_source = td.utf8;
    shape.has_pre_shaping_source = true;
    shape.animated_document_hash = hash_text_document(td);
    shape.animated_document_style =
        !td.spans.empty() || !td.glyph_paint_ranges.empty();
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
        shape.dissolve_document_hash = 0;
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
        shape.dissolve_document_hash = 0;
        return false;
    }

    TextDocument cf_td = make_document_for_frame(
        *state.dissolve_from, cf_utf8, effective_font_outgoing);
    const u64 cf_document_hash = hash_text_document(cf_td);

    // Inside the gap: fast-path skip when the outgoing document identity
    // (text, spans and paint ranges) already matches.
    const bool same_layout =
        shape.dissolve_layout
        && shape.dissolve_layout->source_text == cf_utf8
        && shape.dissolve_document_hash == cf_document_hash;

    if (same_layout && shape.dissolve_mix == state.mix) {
        // mix already tracked; cache lookup would hit anyway.
        shape.dissolve_mix = state.mix;  // idempotent refresh
        return false;
    }

    auto cf_merged = build_merged_layout(
        cf_td, layout_spec, engine, (cache != nullptr) ? cache : shape.cache);
    if (cf_merged) {
        apply_glyph_paint_ranges(*cf_merged, cf_td.glyph_paint_ranges);
        apply_span_fill_overrides(*cf_merged, cf_td.spans);
        shape.dissolve_layout = std::move(cf_merged);
        shape.dissolve_document_hash = cf_document_hash;
        reset_glyph_states_in_place(shape.dissolve_glyphs, shape.dissolve_layout->placed);
        shape.dissolve_mix = state.mix;
        return true;
    }
    // Build failed (e.g. font load failure) — clear slot rather than
    // leave stale state.
    shape.dissolve_layout.reset();
    shape.dissolve_glyphs.clear();
    shape.dissolve_mix = 0.0f;
    shape.dissolve_document_hash = 0;
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

    // Active-side prewarm (empty is valid for typewriter frame 0)
    FontSpec effective_font = compute_effective_font_for_prewarm(state, shape.layout.get());
    TextDocument td = make_document_for_frame(
        *state.active, target_text, effective_font);

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
        TextDocument cf_td = make_document_for_frame(
            *state.dissolve_from, state.dissolve_from->utf8,
            state.dissolve_from->defaults.font);
        TextLayoutRequest cf_request{&cf_td, &layout_spec, cf_td.defaults.font, 0};
        cf_request.features = layout_spec.features;
        TextCompileServices cf_services{&engine, cache_ptr, {}};
        cf_ok = static_cast<bool>(
            compile_text_document_layout(cf_request, cf_services));
    }

    return active_ok && cf_ok;
}

}  // namespace chronon3d::text::driver
