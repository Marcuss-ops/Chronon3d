// SPDX-License-Identifier: MIT
//
// Canonical text layout entry points. Pipeline stage implementations and
// document accumulation live under src/text/compiler/; this TU only orders
// those stages and preserves the public build_text_run compatibility wrapper.

#include <chronon3d/text/text_run_builder.hpp>

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/text/glyph_selector.hpp>

#include "compiler/text_compile_internal.hpp"
#include "compiler/text_compile_orchestrator_helpers.hpp"
#include "text_document_compile_result.hpp"

#include <spdlog/spdlog.h>

#include <memory>
#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

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
    ParagraphStyle paragraph_style =
        tci::effective_paragraph_style(paragraph, layout);
    TextLayoutRequest effective_request = request;
    for (const auto& axis : paragraph_style.variable_axes) {
        if (axis.value == -1.0f) continue;
        if (!effective_request.variation_axes.empty()) effective_request.variation_axes += ',';
        effective_request.variation_axes += axis.tag + '=' + std::to_string(axis.value);
    }
    const bool is_single_font = paragraph.runs.size() == 1;

    if (!tci::check_paragraph_has_font(paragraph.runs)) {
        return make_text_layout_error(
            TextLayoutErrorKind::MissingFont,
            "compile_text_layout: no usable font resolved for paragraph");
    }

    std::string paragraph_text = tci::build_paragraph_text(paragraph.runs);
    if (auto cached = tci::try_cache_lookup(
            services.cache,
            is_single_font,
            paragraph_text,
            primary_font,
            layout,
            effective_request)) {
        return cached;
    }

    // 3–4. Shape each run and apply the configured failure policy.
    // TICKET-TEXT-TIMING-V1 — time the non-cached layout compile (shape +
    // bidi + compose + align + cache store) so a per-frame re-layout
    // regression is visible.  The cache-hit path returns above and is
    // effectively free, so it is deliberately excluded.
    const auto layout_start = profiling::now();
    auto per_run_results = tci::shape_paragraph_runs(
        paragraph.runs,
        engine,
        layout.tracking,
        effective_request.features,
        effective_request.variation_axes);
    auto placed_result = tci::apply_failure_policy(
        std::move(per_run_results),
        effective_request.shaping_failure_policy);
    if (!placed_result) return placed_result.error();
    std::vector<PlacedGlyphRun> placed_runs = std::move(placed_result.value());

    PlacedGlyphRun merged = tci::concatenate_runs(placed_runs);
    if (auto check = tci::validate_concatenated_run(merged, paragraph_text); !check) {
        return check.error();
    }

    // 5. Compose and vertically align positioned glyphs.
    switch (layout.align) {
    case TextAlign::Center:
        paragraph_style.justification = TextJustification::Center;
        break;
    case TextAlign::Right:
        paragraph_style.justification = TextJustification::Right;
        break;
    case TextAlign::Left:
    default:
        paragraph_style.justification = TextJustification::Left;
        break;
    }
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
    tci::populate_font_spans(*text_layout, placed_runs, paragraph.runs,
                             effective_request.variation_axes);
    tci::finalize_text_run_layout(
        *text_layout,
        composed,
        layout,
        paragraph_style,
        effective_request);

    // 7. Cache single-font layouts only.
    tci::store_in_cache(
        services.cache,
        is_single_font,
        paragraph_text,
        primary_font,
        layout,
        effective_request,
        text_layout);

    // TICKET-TEXT-TIMING-V1 — accumulate layout wall time on the success
    // path only (error returns above skip attribution, matching shaping).
    if (profiling::g_current_counters) {
        profiling::g_current_counters->text_layout_wall_ms.fetch_add(
            static_cast<uint64_t>(std::llround(profiling::elapsed_ms(layout_start))),
            std::memory_order_relaxed);
    }

    return text_layout;
}

// ═══════════════════════════════════════════════════════════════════════
// Multi-paragraph composition — every paragraph in ONE TextRunLayout
// ═══════════════════════════════════════════════════════════════════════
//
// Bug this exists for: `\n` (and U+2029) split the document into
// paragraphs, `compile_text_layout(request, …)` compiles exactly ONE of
// them (`request.paragraph_index`), and every consumer of a text run
// holds a single `TextRunLayout`.  Long phrases — which the semantic
// compiler hard-wraps every `phraseWrapCols` characters by injecting `\n`
// — therefore rendered only their FIRST line.

namespace {

/// A paragraph that compiled successfully, tagged with its ordinal in
/// `TextDocument::paragraphs` (needed to rebase paragraph-relative byte
/// offsets onto the full document text).
struct StackedParagraph {
    std::size_t index{0};
    std::shared_ptr<TextRunLayout> layout;
};

/// Stack per-paragraph layouts into ONE composed TextRunLayout.
///
/// Every `part` must have been compiled with `VerticalAlign::Top`, so its
/// glyphs are laid out from y = 0 and `part.bounds.y` is exactly the
/// vertical space it occupies.  Paragraph N is shifted down by the summed
/// height of paragraphs 0..N-1; the document-level vertical alignment is
/// then applied to the whole block through the SAME helper the
/// single-paragraph path uses, so stacked text lands where a wrapped
/// multi-line paragraph would land.
///
/// `source_text` becomes the FULL document text (`split_paragraphs()`
/// strips the separators, so re-joining with `\n` reconstructs `doc.utf8`
/// byte-for-byte).  Glyph `byte_offset` / `cluster` and cluster ranges are
/// rebased onto that document-relative origin so `build_text_unit_map`,
/// the glyph selectors, and the per-frame fast-path identity check keep
/// working against the whole phrase.
std::shared_ptr<TextRunLayout> stack_paragraph_layouts(
    const TextDocument& doc,
    const std::vector<StackedParagraph>& parts,
    const TextLayoutSpec& layout
) {
    if (parts.empty()) {
        return nullptr;
    }

    const TextRunLayout& first = *parts.front().layout;

    auto merged = std::make_shared<TextRunLayout>();
    merged->source_text    = doc.utf8;
    merged->font           = first.font;
    merged->font_size      = first.font_size;
    merged->tracking       = first.tracking;
    merged->wrap           = first.wrap;
    merged->direction      = first.direction;
    merged->language       = first.language;
    merged->features       = first.features;
    merged->variation_axes = first.variation_axes;
    merged->line_height    = first.line_height;

    PlacedGlyphRun& out = merged->placed;
    out.font_size = first.placed.font_size;
    out.ascent    = first.placed.ascent;
    out.descent   = first.placed.descent;
    out.baseline  = first.placed.baseline;

    float y_offset = 0.0f;
    float max_width = 0.0f;
    std::size_t glyph_cursor = 0;

    for (std::size_t pi = 0; pi < parts.size(); ++pi) {
        const StackedParagraph& part = parts[pi];
        const TextRunLayout& src = *part.layout;
        const std::size_t base =
            (part.index < doc.paragraphs.size())
                ? doc.paragraphs[part.index].byte_start
                : 0;

        out.glyphs.reserve(out.glyphs.size() + src.placed.glyphs.size());
        for (const PlacedGlyph& g : src.placed.glyphs) {
            PlacedGlyph rebased = g;
            rebased.y           += y_offset;
            rebased.byte_offset += base;
            rebased.cluster      = static_cast<u32>(rebased.cluster + base);
            out.glyphs.push_back(rebased);
        }

        out.clusters.reserve(out.clusters.size() + src.placed.clusters.size());
        for (const PlacedGlyphRun::Cluster& c : src.placed.clusters) {
            PlacedGlyphRun::Cluster rebased = c;
            rebased.start_glyph += glyph_cursor;
            rebased.end_glyph   += glyph_cursor;
            rebased.byte_offset += base;
            out.clusters.push_back(rebased);
        }

        merged->font_spans.reserve(merged->font_spans.size() + src.font_spans.size());
        for (const ShapedFontSpan& s : src.font_spans) {
            ShapedFontSpan rebased = s;
            rebased.glyph_begin = static_cast<std::uint32_t>(
                static_cast<std::size_t>(rebased.glyph_begin) + glyph_cursor);
            rebased.glyph_end = static_cast<std::uint32_t>(
                static_cast<std::size_t>(rebased.glyph_end) + glyph_cursor);
            merged->font_spans.push_back(rebased);
        }

        glyph_cursor += src.placed.glyphs.size();
        max_width     = std::max(max_width, src.bounds.x);
        // An empty paragraph (a blank line produced by consecutive `\n`) has
        // zero-height bounds but still occupies one line of vertical space —
        // without this the following paragraph would be pulled up onto it.
        // A trailing blank line is not measured: it has no following content.
        const bool blank_line_before_more =
            pi + 1 < parts.size() && src.placed.glyphs.empty() &&
            src.bounds.y <= 0.0f && src.line_height > 0.0f;
        y_offset += blank_line_before_more ? src.line_height : src.bounds.y;
    }

    out.total_width  = max_width;
    out.total_height = y_offset;

    // Document-level vertical alignment of the WHOLE stacked block.
    ParagraphLayout block;
    block.bounds = Vec2{max_width, y_offset};
    tci::apply_vertical_alignment(out, block, layout);
    merged->bounds = block.bounds;

    merged->units = build_text_unit_map(merged->placed, merged->source_text);
    return merged;
}

} // namespace

Result<std::shared_ptr<TextRunLayout>, TextLayoutError>
compile_text_document_layout(
    const TextLayoutRequest& request,
    TextCompileServices& services,
    const ResolvedTextTree* pre_resolved
) {
    // ── Single-paragraph documents keep the historical path untouched. ──
    // (byte-for-byte identical output; no merge pass, no extra work)
    if (request.doc == nullptr || request.layout == nullptr ||
        request.doc->paragraphs.size() <= 1) {
        return compile_text_layout(request, services, pre_resolved);
    }

    if (services.engine == nullptr) {
        return make_text_layout_error(
            TextLayoutErrorKind::ShapingFailed,
            "compile_text_document_layout: services.engine is null");
    }

    // ── Compile every paragraph TOP-aligned so each one stacks from y = 0. ──
    // The real vertical alignment is applied once to the merged block
    // (see stack_paragraph_layouts) instead of once per paragraph —
    // per-paragraph centering would overlap every paragraph on the
    // same baseline.
    TextLayoutSpec paragraph_layout = *request.layout;
    paragraph_layout.vertical_align = VerticalAlign::Top;

    ResolvedTextTree local_tree;
    if (pre_resolved == nullptr) {
        local_tree = resolve_text_run_tree(
            *request.doc, *services.engine, services.bundled_fonts_root);
        pre_resolved = &local_tree;
    }
    if (pre_resolved->paragraphs.empty()) {
        return make_text_layout_error(
            TextLayoutErrorKind::EmptySource,
            "compile_text_document_layout: resolved tree has no paragraphs");
    }

    const std::size_t paragraph_count =
        std::min(request.doc->paragraphs.size(), pre_resolved->paragraphs.size());

    std::vector<StackedParagraph> parts;
    parts.reserve(paragraph_count);
    std::optional<TextLayoutError> first_error;

    for (std::size_t i = 0; i < paragraph_count; ++i) {
        TextLayoutRequest paragraph_request = request;
        paragraph_request.layout          = &paragraph_layout;
        paragraph_request.paragraph_index = i;

        auto paragraph = compile_text_layout(paragraph_request, services, pre_resolved);
        if (paragraph && paragraph.value()) {
            parts.push_back(StackedParagraph{i, std::move(paragraph.value())});
            continue;
        }

        const TextLayoutError error = paragraph
            ? make_text_layout_error(
                  TextLayoutErrorKind::ShapingProducedNoGlyphs,
                  "compile_text_document_layout: paragraph " + std::to_string(i) +
                      " produced no layout")
            : paragraph.error();
        if (!first_error) {
            first_error = error;
        }
        spdlog::warn(
            "compile_text_document_layout: paragraph {} skipped — {}",
            i, error.message);
    }

    if (parts.empty()) {
        return first_error.value_or(make_text_layout_error(
            TextLayoutErrorKind::EmptySource,
            "compile_text_document_layout: no paragraph compiled"));
    }

    auto merged = stack_paragraph_layouts(*request.doc, parts, *request.layout);
    if (!merged) {
        return make_text_layout_error(
            TextLayoutErrorKind::ShapingProducedNoGlyphs,
            "compile_text_document_layout: failed to stack paragraph layouts");
    }
    return merged;
}

// X2 core dispatch (defined below): compile a PreparedText WITHOUT re-entering
// the auto-fit entry point.  The ADR-018 probe loop must call this, not
// compile_text_layout(PreparedText): a probe inherits the Clip overflow policy,
// so routing probes through the auto-fit entry would recurse until stack
// overflow.
static Result<std::shared_ptr<TextRunLayout>, TextLayoutError>
compile_prepared_text_core(
    const PreparedText& effective,
    TextCompileServices& services,
    const ResolvedTextTree* pre_resolved
);

Result<std::shared_ptr<TextRunLayout>, TextLayoutError>
compile_text_layout(
    const PreparedText& prepared,
    TextCompileServices& services,
    const ResolvedTextTree* pre_resolved
) {
    // Keep authoring auto-fit on the canonical PreparedText path as well as
    // the compatibility PreparedText path.  Probe through the same
    // compiler with auto-fit disabled, then compile the selected size once.
    //
    // Clipping controls which glyph pixels are visible; it does not authorize
    // changing the authored font size.  Auto-fit remains opt-in through the
    // explicit frame flag.  Treating every positive Clip box as auto-fit can
    // shrink ordinary overlay titles to min_font_size when layout bounds
    // include box metrics instead of ink bounds.
    const bool auto_fit_required = prepared.frame.auto_fit ||
                                   prepared.frame.grow_and_shrink;
    PreparedText effective = prepared;
    if (auto_fit_required) {
        const f32 min_fs = prepared.frame.min_font_size;
        const f32 max_fs = std::min(prepared.style.font.font_size,
                                    prepared.frame.max_font_size);
        if (max_fs > min_fs) {
            const auto fits = [&](f32 size) {
                PreparedText probe = prepared;
                probe.frame.auto_fit = false;
                probe.style.font.font_size = size;
                auto result = compile_prepared_text_core(probe, services, pre_resolved);
                // Font metrics exclude part of the raster ink (notably
                // ascender/descender overshoot), so reserve a small
                // deterministic vertical safety margin before accepting a
                // probe.  This keeps the authoring contract true for the
                // rendered alpha bounds, not only the layout metrics.
                return result &&
                       result.value()->bounds.x <= prepared.frame.size.x * 0.95f &&
                       (prepared.frame.grow_and_shrink ||
                        result.value()->bounds.y <= prepared.frame.size.y * 0.75f);
            };
            f32 low = min_fs;
            f32 high = max_fs;
            f32 best = min_fs;
            if (fits(max_fs)) {
                best = max_fs;
            } else {
                for (int i = 0; i < 12; ++i) {
                    const f32 mid = (low + high) * 0.5f;
                    if (fits(mid)) {
                        best = mid;
                        low = mid;
                    } else {
                        high = mid;
                    }
                }
            }
            effective.frame.auto_fit = false;
            effective.frame.grow_and_shrink = false;
            effective.style.font.font_size = best;
            effective.document.defaults.font.font_size = best;
        }
    }

    if (prepared.frame.grow_and_shrink) {
        PreparedText measure = effective;
        measure.frame.auto_fit = false;
        measure.frame.grow_and_shrink = false;
        measure.frame.vertical_align = VerticalAlign::Top;
        // The first pass measures wrapped content with the authored width and
        // no vertical clip. The second pass below uses the measured height so
        // vertical alignment and clipping are resolved against the final box.
        measure.frame.size.y = 1'000'000.0f;
        auto measured = compile_prepared_text_core(measure, services, pre_resolved);
        if (measured) {
            const float padding = effective.style.box_style.padding.y * 2.0f;
            effective.frame.size.y = std::max(
                1.0f, measured.value()->bounds.y + padding);
        }
    }

    return compile_prepared_text_core(effective, services, pre_resolved);
}

// X2 canonical path: synthesise a transient TextLayoutSpec from the
// canonical TextFrame and dispatch to the existing request-based
// implementation.  This keeps the compiler's internal logic unchanged
// while exposing the canonical PreparedText API to callers.
static Result<std::shared_ptr<TextRunLayout>, TextLayoutError>
compile_prepared_text_core(
    const PreparedText& effective,
    TextCompileServices& services,
    const ResolvedTextTree* pre_resolved
) {
    TextLayoutSpec layout;
    layout.box = effective.frame.size;
    layout.anchor = effective.frame.anchor;
    layout.centering_mode = effective.frame.centering_mode;
    layout.align = effective.frame.align;
    layout.vertical_align = effective.frame.vertical_align;
    layout.wrap = effective.frame.wrap;
    layout.overflow = effective.frame.overflow;
    layout.line_height = effective.frame.line_height;
    layout.tracking = effective.frame.tracking;
    layout.auto_fit = effective.frame.auto_fit;
    layout.min_font_size = effective.frame.min_font_size;
    layout.max_font_size = effective.frame.max_font_size;
    layout.max_lines = effective.frame.max_lines;
    layout.ellipsis = effective.frame.ellipsis;
    layout.paragraph = effective.document.paragraphs.empty()
        ? ParagraphStyle{}
        : effective.document.paragraphs.front().style;
    layout.features = effective.shaping.open_type_features;

    TextLayoutRequest request{
        &effective.document,
        &layout,
        effective.style.font,
        0,
    };
    request.direction = effective.shaping.direction;
    request.language = effective.shaping.language;
    request.features = effective.shaping.open_type_features;

    // Multi-paragraph documents (`\n`-separated) must compile into ONE
    // composed layout — routing through the single-paragraph compiler
    // silently dropped every paragraph after the first.
    return compile_text_document_layout(request, services, pre_resolved);
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
