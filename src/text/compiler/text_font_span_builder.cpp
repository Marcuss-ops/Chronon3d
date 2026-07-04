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

#include <cstdint>
#include <spdlog/spdlog.h>

namespace chronon3d::text_internal::compile {

void
populate_font_spans(
    TextRunLayout& text_layout,
    const std::vector<PlacedGlyphRun>& placed_runs,
    const std::vector<ResolvedTextRun>& para_runs
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
        span.glyph_begin = begin;
        span.glyph_end   = end;
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
}

} // namespace chronon3d::text_internal::compile
