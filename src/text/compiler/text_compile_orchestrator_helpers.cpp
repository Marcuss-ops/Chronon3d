// SPDX-License-Identifier: MIT

#include "text_compile_orchestrator_helpers.hpp"

#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/text/text_resolver.hpp>

#include <cmath>
#include <utility>

namespace chronon3d::text_internal::compile {

std::shared_ptr<TextRunLayout>
build_empty_paragraph_layout(const TextDocument& doc) {
    auto layout = std::make_shared<TextRunLayout>();
    layout->source_text.clear();
    layout->font = doc.defaults.font;
    layout->font_size = doc.defaults.font.font_size;
    layout->bounds = {0.0f, 0.0f};
    layout->line_height = doc.defaults.font.font_size * 1.2f;
    layout->units = build_text_unit_map(PlacedGlyphRun{}, std::string{});
    return layout;
}

Result<const ResolvedParagraph*, TextLayoutError>
resolve_target_paragraph(
    const TextDocument& doc,
    FontEngine& engine,
    const ResolvedTextTree* pre_resolved,
    std::size_t paragraph_index,
    ResolvedTextTree& local_storage
) {
    const ResolvedTextTree& tree = pre_resolved
        ? *pre_resolved
        : (local_storage = resolve_text_run_tree(doc, engine), local_storage);

    if (tree.paragraphs.empty()) {
        return TextLayoutError{
            TextLayoutErrorKind::EmptySource,
            "resolve_target_paragraph: resolved tree has no paragraphs"
        };
    }
    if (paragraph_index >= tree.paragraphs.size()) {
        return TextLayoutError{
            TextLayoutErrorKind::MalformedLayout,
            "resolve_target_paragraph: paragraph_index out of range"
        };
    }
    return &tree.paragraphs[paragraph_index];
}

std::shared_ptr<TextRunLayout>
try_cache_lookup(
    TextLayoutCache* cache,
    bool is_single_font,
    const std::string& paragraph_text,
    const FontSpec& primary_font,
    const TextLayoutSpec& layout,
    const TextLayoutRequest& request
) {
    if (!cache || !is_single_font) return nullptr;
    TextLayoutCacheKey key = build_cache_key(
        paragraph_text,
        primary_font,
        layout,
        request.direction,
        request.language,
        request.features,
        request.variation_axes);
    if (auto cached = cache->find(key)) {
        return std::const_pointer_cast<TextRunLayout>(cached);
    }
    return nullptr;
}

void store_in_cache(
    TextLayoutCache* cache,
    bool is_single_font,
    const std::string& paragraph_text,
    const FontSpec& primary_font,
    const TextLayoutSpec& layout,
    const TextLayoutRequest& request,
    const std::shared_ptr<TextRunLayout>& text_layout
) {
    if (!cache || !is_single_font) return;
    TextLayoutCacheKey key = build_cache_key(
        paragraph_text,
        primary_font,
        layout,
        request.direction,
        request.language,
        request.features,
        request.variation_axes);
    cache->store(std::move(key), text_layout);
}

ParagraphStyle effective_paragraph_style(
    const ResolvedParagraph& paragraph,
    const TextLayoutSpec& layout
) {
    ParagraphStyle style = paragraph.style == ParagraphStyle{}
        ? layout.paragraph
        : paragraph.style;

    // Word wrapping is the bounded, greedy layout contract exposed by
    // TextLayoutSpec.  Keep the advanced global composer opt-in; otherwise
    // a resolved default paragraph style can bypass the width-aware greedy
    // breaker and emit one unbounded line.
    if (layout.wrap == TextWrap::Word) {
        style.composer = ParagraphComposer::SingleLine;
    }
    return style;
}

void apply_vertical_alignment(
    PlacedGlyphRun& placed,
    ParagraphLayout& composed,
    const TextLayoutSpec& layout
) {
    if (layout.box.y <= 0.0f || layout.vertical_align == VerticalAlign::Top) {
        return;
    }

    const float text_height = composed.bounds.y;
    if (!std::isfinite(text_height) || layout.box.y <= text_height) {
        return;
    }

    float delta_y = 0.0f;
    if (layout.vertical_align == VerticalAlign::Middle) {
        delta_y = (layout.box.y - text_height) * 0.5f;
    } else if (layout.vertical_align == VerticalAlign::Bottom) {
        delta_y = layout.box.y - text_height;
    }
    if (delta_y <= 0.0f) return;

    for (auto& glyph : placed.glyphs) {
        glyph.y += delta_y;
    }
    composed.bounds.y += delta_y;
}

void finalize_text_run_layout(
    TextRunLayout& text_layout,
    const ParagraphLayout& composed,
    const TextLayoutSpec& layout,
    const ParagraphStyle& paragraph_style,
    const TextLayoutRequest& request
) {
    text_layout.bounds = composed.bounds;
    text_layout.tracking = layout.tracking;
    text_layout.wrap = layout.wrap;
    text_layout.direction = paragraph_style.direction;
    text_layout.language = paragraph_style.language;
    text_layout.line_height = layout.line_height;
    text_layout.features = request.features;
    text_layout.variation_axes = request.variation_axes;
}

} // namespace chronon3d::text_internal::compile
