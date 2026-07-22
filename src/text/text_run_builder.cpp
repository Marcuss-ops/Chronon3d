// SPDX-License-Identifier: MIT
//
// Canonical text layout entry points. Pipeline stage implementations and
// document accumulation live under src/text/compiler/; this TU only orders
// those stages and preserves the public build_text_run compatibility wrapper.

#include <chronon3d/text/text_run_builder.hpp>

#include <chronon3d/text/glyph_selector.hpp>

#include "compiler/text_compile_internal.hpp"
#include "compiler/text_compile_orchestrator_helpers.hpp"
#include "text_document_compile_result.hpp"

#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

namespace chronon3d {

namespace tci = text_internal::compile;

Result<std::shared_ptr<TextRunLayout>, TextLayoutError>
compile_text_layout(
    const TextLayoutRequest& request,
    TextCompileServices& services,
    const ResolvedTextTree* pre_resolved
) {
    // 1. Validate request and services.
    auto validated = tci::validate_layout_request(request, services);
    if (!validated) return validated.error();
    auto& refs = validated.value();
    const TextDocument& doc = refs.doc;
    const TextLayoutSpec& layout = refs.layout;
    FontEngine& engine = refs.engine;

    // 2. Resolve the requested paragraph once.
    ResolvedTextTree local_storage{};
    auto paragraph_result = tci::resolve_target_paragraph(
        doc,
        services,
        pre_resolved,
        request.paragraph_index,
        local_storage);
    if (!paragraph_result) return paragraph_result.error();
    const ResolvedParagraph& paragraph = *paragraph_result.value();

    if (paragraph.runs.empty()) {
        return tci::build_empty_paragraph_layout(doc);
    }

    const FontSpec primary_font = request.primary_font.font_path.empty()
        ? doc.defaults.font
        : request.primary_font;
    const bool is_single_font = paragraph.runs.size() == 1;

    if (!tci::check_paragraph_has_font(paragraph.runs)) {
        return TextLayoutError{
            TextLayoutErrorKind::MissingFont,
            "compile_text_layout: no usable font resolved for paragraph"
        };
    }

    std::string paragraph_text = tci::build_paragraph_text(paragraph.runs);
    if (auto cached = tci::try_cache_lookup(
            services.cache,
            is_single_font,
            paragraph_text,
            primary_font,
            layout,
            request)) {
        return cached;
    }

    // 3–4. Shape each run and apply the configured failure policy.
    auto per_run_results = tci::shape_paragraph_runs(
        paragraph.runs,
        engine,
        layout.tracking,
        request.features);
    auto placed_result = tci::apply_failure_policy(
        std::move(per_run_results),
        request.shaping_failure_policy);
    if (!placed_result) return placed_result.error();
    std::vector<PlacedGlyphRun> placed_runs = std::move(placed_result.value());

    PlacedGlyphRun merged = tci::concatenate_runs(placed_runs);
    if (auto check = tci::validate_concatenated_run(merged, paragraph_text); !check) {
        return check.error();
    }

    // 5. Compose and vertically align positioned glyphs.
    const ParagraphStyle paragraph_style =
        tci::effective_paragraph_style(paragraph, layout);
    ParagraphLayout composed = tci::compose_paragraph(
        merged,
        layout.box.x - paragraph_style.left_indent - paragraph_style.right_indent,
        paragraph_style,
        paragraph_text);
    tci::apply_composition_to_placed(merged, composed);
    tci::apply_vertical_alignment(merged, composed, layout);

    auto text_layout = std::make_shared<TextRunLayout>();
    text_layout->source_text = paragraph_text;
    text_layout->font = primary_font;
    text_layout->font_size = primary_font.font_size;
    text_layout->placed = std::move(merged);
    text_layout->units = build_text_unit_map(text_layout->placed, paragraph_text);

    // 6. Preserve per-run font identities before the immutable cache store.
    tci::populate_font_spans(*text_layout, placed_runs, paragraph.runs);
    tci::finalize_text_run_layout(
        *text_layout,
        composed,
        layout,
        paragraph_style,
        request);

    // 7. Cache single-font layouts only.
    tci::store_in_cache(
        services.cache,
        is_single_font,
        paragraph_text,
        primary_font,
        layout,
        request,
        text_layout);

    return text_layout;
}

TextRunBuildResult build_text_run(
    const TextDocument& doc,
    FontEngine& engine,
    const TextLayoutSpec& layout,
    TextLayoutCache* cache,
    const std::filesystem::path& bundled_fonts_root
) {
    TextRunBuildResult result;
    if (doc.utf8.empty() && doc.paragraphs.empty()) {
        return result;
    }

    auto compiled = text_internal::compile_text_document(
        doc,
        engine,
        layout,
        cache,
        bundled_fonts_root);
    result.complete = compiled.complete;
    result.missing_glyph_count = compiled.missing_glyph_count;

    for (auto& paragraph : compiled.paragraphs) {
        if (paragraph.result) {
            result.paragraphs.push_back(
                std::const_pointer_cast<TextRunLayout>(paragraph.result.value()));
        } else {
            spdlog::warn(
                "build_text_run: paragraph {} skipped — {}",
                paragraph.source_index,
                paragraph.result.error().message);
        }
    }

    return result;
}

} // namespace chronon3d
