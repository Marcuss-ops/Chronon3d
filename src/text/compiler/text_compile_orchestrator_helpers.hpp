// SPDX-License-Identifier: MIT
#pragma once

#include "text_compile_internal.hpp"

namespace chronon3d::text_internal::compile {

[[nodiscard]] std::shared_ptr<TextRunLayout>
build_empty_paragraph_layout(const TextDocument& doc);

[[nodiscard]] Result<const ResolvedParagraph*, TextLayoutError>
resolve_target_paragraph(
    const TextDocument& doc,
    const TextCompileServices& services,
    const ResolvedTextTree* pre_resolved,
    std::size_t paragraph_index,
    ResolvedTextTree& local_storage
);

[[nodiscard]] std::shared_ptr<TextRunLayout>
try_cache_lookup(
    TextLayoutCache* cache,
    bool is_single_font,
    const std::string& paragraph_text,
    const FontSpec& primary_font,
    const TextLayoutSpec& layout,
    const TextLayoutRequest& request
);

void store_in_cache(
    TextLayoutCache* cache,
    bool is_single_font,
    const std::string& paragraph_text,
    const FontSpec& primary_font,
    const TextLayoutSpec& layout,
    const TextLayoutRequest& request,
    const std::shared_ptr<TextRunLayout>& text_layout
);

[[nodiscard]] ParagraphStyle effective_paragraph_style(
    const ResolvedParagraph& paragraph,
    const TextLayoutSpec& layout
);

void apply_vertical_alignment(
    PlacedGlyphRun& placed,
    ParagraphLayout& composed,
    const TextLayoutSpec& layout
);

void finalize_text_run_layout(
    TextRunLayout& text_layout,
    const ParagraphLayout& composed,
    const TextLayoutSpec& layout,
    const ParagraphStyle& paragraph_style,
    const TextLayoutRequest& request
);

} // namespace chronon3d::text_internal::compile
