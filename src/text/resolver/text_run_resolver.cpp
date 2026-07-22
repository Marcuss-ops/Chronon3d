// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/text/resolver/text_run_resolver.cpp — orchestrator
//
// M1.5#8 — single-responsibility module that owns the canonical public
// entry point for text resolution:
//
//     ResolvedTextTree resolve_text_run_tree(const TextDocument&, FontEngine&)
//
// The orchestrator delegates the three sub-responsibilities to:
//
//   * `split_by_font(...)`           — text_span_resolver.cpp
//   * `FontResolver::resolve(...)`   — text_font_resolver.cpp (single service)
//   * `emit_via_bidi(...)`           — text_bidi_resolver.cpp
//   * `shape_resolved_run(...)`      — HERE (uses FontEngine directly)
//   * `resolve_and_shape(...)`       — HERE (composes resolve + shape)
//
// What lives here:
//   * `resolve_text_run_tree(doc, engine)` — the unico entry pubblico.
//   * `shape_resolved_run(run, engine, tracking)` — single-run shaping.
//   * `resolve_and_shape(doc, engine, tracking)` — resolve + shape chain.
//
// What does NOT live here:
//   * Font fallback chain (text_font_resolver.cpp / FontResolver service).
//   * Span splitting (text_span_resolver.cpp).
//   * Bidi segmentation per sub-range (text_bidi_resolver.cpp).
//
// BCP-47 canonicalization at the source is performed here (TICKET-101
// follow-up), via src/text/internal/text_resolver_helpers.hpp.

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_resolver.hpp>    // ResolvedTree types + ParagraphRange
#include <chronon3d/text/text_document.hpp>

#include "src/text/internal/text_resolver_helpers.hpp"
#include "src/text/resolver/text_span_resolver.hpp"
#include "src/text/resolver/text_bidi_resolver.hpp"

#include <filesystem>
#include <utility>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// Public API: resolve_text_run_tree (the canonical orchestrator entry)
// ═══════════════════════════════════════════════════════════════════════════

ResolvedTextTree resolve_text_run_tree(
    const TextDocument& doc,
    FontEngine& engine,
    const std::filesystem::path& bundled_fonts_root
) {
    ResolvedTextTree tree;

    if (doc.utf8.empty() && doc.paragraphs.empty()) {
        return tree;
    }

    // The caller MUST call doc.split_paragraphs() before passing to
    // resolve_text_run_tree().  (split_paragraphs() is non-const, so
    // the resolver cannot auto-split on a const reference.)  If the
    // caller forgot, we return an empty tree deterministically.
    if (doc.paragraphs.empty()) {
        return tree;
    }

    const auto& paragraphs = doc.paragraphs;

    for (const auto& para : paragraphs) {
        ResolvedParagraph resolved_para;
        // TICKET-101 follow-up — canonicalize the BCP-47 language tag
        // at the SOURCE so all downstream consumers (run.paragraph_style,
        // shaped_para.style) see the same canonical value.  Empty /
        // invalid tags fall through to HarfBuzz auto-detection.
        resolved_para.style = para.style;
        if (chronon3d::text::internal::validate_bcp47_language_tag(para.style.language)) {
            resolved_para.style.language =
                chronon3d::text::internal::canonicalize_bcp47_language_tag(para.style.language);
        }

        // Split paragraph byte range by font changes (text_span_resolver)
        // → emits a list of homogeneous sub-ranges.
        auto sub_ranges = chronon3d::text::resolver::split_by_font(
            doc, para.byte_start, para.byte_end);

        // For each font-homogeneous sub-range, run bidi + emit runs
        // (text_bidi_resolver).  The bidi resolver internally calls
        // FontResolver (text_font_resolver) for the fallback chain.
        //
        for (const auto& sub : sub_ranges) {
            tree.missing_glyph_count += chronon3d::text::resolver::emit_via_bidi(
                resolved_para.runs,
                doc, engine, sub, para,
                para.style.direction,
                bundled_fonts_root);
        }

        // A paragraph may have no runs (empty paragraph — e.g. two
        // consecutive \n).  Keep it as an empty paragraph in the tree
        // to preserve paragraph count for layout.
        tree.paragraphs.push_back(std::move(resolved_para));
    }

    return tree;
}


// ═══════════════════════════════════════════════════════════════════════════
// Public API: shape_resolved_run (single-run HarfBuzz shaping)
// ═══════════════════════════════════════════════════════════════════════════

PlacedGlyphRun shape_resolved_run(
    const ResolvedTextRun& run,
    FontEngine& engine,
    float tracking,
    const std::string& features
) {
    TextShaping shaping;
    shaping.direction = run.direction;
    shaping.features = features;

    // TICKET-101 follow-up — defense-in-depth: canonicalize the
    // BCP-47 language tag before forwarding to HarfBuzz.  The
    // canonical value is ALREADY in resolved_para.style.language
    // (canonicalized at the source in resolve_text_run_tree), so
    // this is redundant for the resolve_text_run_tree → shape path.
    // It protects direct shape_resolved_run() callers that bypass
    // resolve_text_run_tree (e.g., tests, internal pipelines).
    const std::string& raw_lang = run.paragraph_style.language;
    if (chronon3d::text::internal::validate_bcp47_language_tag(raw_lang)) {
        shaping.language =
            chronon3d::text::internal::canonicalize_bcp47_language_tag(raw_lang);
    }

    auto hb_run = engine.shape_text(
        run.text,
        run.font,
        run.font.font_size,
        shaping
    );

    if (!hb_run) {
        return PlacedGlyphRun{};
    }

    auto placed = resolve_placed_glyph_run(*hb_run, tracking, run.text);
    placed.ascent       = hb_run->ascent;
    placed.descent      = hb_run->descent;
    placed.baseline     = hb_run->baseline;
    placed.total_height = hb_run->ascent + hb_run->descent;
    placed.font_size    = hb_run->font_size;

    return placed;
}

// ═══════════════════════════════════════════════════════════════════════════
// Public API: resolve_and_shape (resolve + shape convenience wrapper)
// ═══════════════════════════════════════════════════════════════════════════

ShapedTextTree resolve_and_shape(
    const TextDocument& doc,
    FontEngine& engine,
    float tracking,
    const std::filesystem::path& bundled_fonts_root,
    const std::string& features
) {
    ShapedTextTree result;

    auto tree = resolve_text_run_tree(doc, engine, bundled_fonts_root);

    for (const auto& para : tree.paragraphs) {
        ShapedParagraph shaped_para;
        shaped_para.style = para.style;
        shaped_para.shaped_runs.reserve(para.runs.size());

        for (const auto& run : para.runs) {
            shaped_para.shaped_runs.push_back(
                shape_resolved_run(run, engine, tracking, features));
        }

        result.paragraphs.push_back(std::move(shaped_para));
    }

    return result;
}

} // namespace chronon3d
