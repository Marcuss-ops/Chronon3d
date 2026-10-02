// SPDX-License-Identifier: MIT
//
// src/text/compiler/text_font_span_builder.cpp — M1.5#5 stage 7.
//
// populate_font_spans:
//   Phase 1.4 — for multi-font paragraphs (canonical font_spans path),
//   walks the shaped placed_runs vector (NOT the merged glyph run)
//   so each run's pre-concatenation index range maps to glyph indices
//   in the merged run via standard cursor arithmetic.  Emits one
//   ShapedFontSpan per run with the corresponding FontIdentity
//   computed via the canonical font_identity_of() projection (zero-
//   overhead, FontSpec minus font_size).  Empty runs (zero shaped
//   glyphs) are skipped — a run with nothing to render would
//   otherwise leave a [N, N) span in the list.
//
//   Diagnostic spdlog::warn on the multi-font anomaly
//   (font_spans.empty() && para.runs.size() > 1) — Phase 1.4
//   surface.  A multi-font input whose shaping produced zero glyphs
//   has collapsed font_spans to empty AND the renderer treats it
//   as an empty paragraph (no glyphs to draw).  Without this warn,
//   a CI box missing the alternate family would silently emit a
//   zero-frame artefact with no diagnostic.

#include "text_compile_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <spdlog/spdlog.h>

namespace chronon3d::text_internal::compile {

void
populate_font_spans(
    TextRunLayout& text_layout,
    const std::vector<PlacedGlyphRun>& placed_runs,
    const std::vector<ResolvedTextRun>& para_runs,
    const std::string& variation_axes
) {
    std::uint32_t glyph_cursor = 0;
    for (std::size_t ri = 0; ri < placed_runs.size(); ++ri) {
        const std::uint32_t begin = glyph_cursor;
        const std::uint32_t end   = glyph_cursor +
            static_cast<std::uint32_t>(placed_runs[ri].glyphs.size());
        glyph_cursor = end;

        if (begin == end) continue;          // skip empty runs
        if (ri >= para_runs.size()) break;    // defensive bound

        ShapedFontSpan span;
        span.font        = font_identity_of(para_runs[ri].font);
        span.variation_axes = variation_axes;
        span.font_size   = para_runs[ri].font.font_size;
        span.glyph_begin = begin;
        span.glyph_end   = end;
        span.fill_color_override = para_runs[ri].fill_color_override;
        span.stroke = para_runs[ri].stroke;
        span.highlight = para_runs[ri].highlight;
        span.semantic_id = para_runs[ri].semantic_id;
        text_layout.font_spans.push_back(std::move(span));
    }

    // A single resolved font with a non-empty merged run is still a valid
    // canonical span even when an older shaping adapter did not preserve its
    // one-element `placed_runs` vector.  Repair that representation at the
    // compiler boundary so every consumer (including DirectYUV) sees the
    // immutable font-range contract; never make a backend guess the font.
    if (text_layout.font_spans.empty() &&
        !text_layout.placed.glyphs.empty() && para_runs.size() == 1) {
        ShapedFontSpan span;
        span.font = font_identity_of(para_runs.front().font);
        span.variation_axes = variation_axes;
        span.font_size = para_runs.front().font.font_size;
        span.glyph_begin = 0;
        span.glyph_end = static_cast<std::uint32_t>(
            text_layout.placed.glyphs.size());
        span.fill_color_override = para_runs.front().fill_color_override;
        span.stroke = para_runs.front().stroke;
        span.highlight = para_runs.front().highlight;
        span.semantic_id = para_runs.front().semantic_id;
        text_layout.font_spans.push_back(std::move(span));
    }

    // ── PHASE 1.4 debug signal ──
    // A multi-font input whose shaping produced zero glyphs has collapsed
    // font_spans to empty AND the renderer treats it as an empty paragraph
    // (no glyphs to draw).  Without this warn, a CI box missing the alternate
    // family would silently emit a zero-frame artefact with no diagnostic.
    // Surface it so the regression is detected by logs + the run-time counters.
    if (text_layout.font_spans.empty() && para_runs.size() > 1) {
        spdlog::warn(
            "compile_text_layout: paragraph resolved into {} ResolvedTextRun(s) "
            "but font_spans is empty after shaping (likely missing system fonts "
            "for one or more identities).  Multi-font input renders as an empty "
            "paragraph — verify fonts are installed on CI.",
            para_runs.size());
    }

    text_layout.units.glyph_semantic_id.assign(text_layout.placed.glyphs.size(), {});
    for (const auto& span : text_layout.font_spans) {
        if (!span.semantic_id) continue;
        const auto end = std::min<std::size_t>(span.glyph_end,
                                               text_layout.units.glyph_semantic_id.size());
        for (std::size_t glyph = span.glyph_begin; glyph < end; ++glyph)
            text_layout.units.glyph_semantic_id[glyph] = *span.semantic_id;
    }
}

} // namespace chronon3d::text_internal::compile
