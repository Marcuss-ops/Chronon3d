// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/text/resolver/text_span_resolver.cpp
//
// M1.5#8 — single-responsibility module responsible for splitting a
// paragraph's byte range into font-homogeneous sub-ranges based on the
// stacking of TextStyleSpan overrides on top of the document defaults.
//
// What lives here:
//   * `effective_font(doc, byte_start)` — applies the LAST covering
//     TextStyleSpan's font override on top of doc.defaults.font.
//   * `split_by_font(doc, para_start, para_end)` — gathers span
//     boundary points in the paragraph range, sorts, dedupes, and emits
//     one FontSubRange per homogeneous segment.
//
// Note: `FontSubRange` itself is canonically declared in
// `text_span_resolver.hpp` (M1.5#8 — visible to text_bidi_resolver and
// text_run_resolver which include the header).  This TU consumes the
// header's type only.
//
// What does NOT live here:
//   * Bidi segmentation (text_bidi_resolver.cpp).
//   * Font fallback (text_font_resolver.cpp / FontResolver service).
//   * Top-level orchestration (text_run_resolver.cpp).

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_document.hpp>
#include <chronon3d/text/text_resolver.hpp>      // ResolvedTextTree + ResolvedParagraph
#include <chronon3d/text/text_span.hpp>
#include "text_span_resolver.hpp"  // M1.5#8 — FontSubRange canonical definition

#include "src/text/internal/text_resolver_helpers.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace chronon3d::text::resolver {

// ── effective_font — apply span override stack on document defaults ──────
//
// Returns a FontSpec for the byte position `byte_start` by walking
// the document's spans in order.  The LAST span covering `byte_start`
// wins (documented override semantics from text_document.hpp).
[[nodiscard]] static FontSpec effective_font(
    const TextDocument& doc,
    std::size_t         byte_start
) {
    // Start with document defaults.
    FontSpec font = doc.defaults.font;

    // Apply the LAST TextStyleSpan that covers this byte.
    for (const auto& span : doc.spans) {
        if (span.byte_start <= byte_start && byte_start < span.byte_end) {
            if (span.font) {
                if (!span.font->font_path.empty())
                    font.font_path = span.font->font_path;
                if (!span.font->font_family.empty())
                    font.font_family = span.font->font_family;
                if (span.font->font_weight != 0)
                    font.font_weight = span.font->font_weight;
                if (!span.font->font_style.empty())
                    font.font_style = span.font->font_style;
                if (span.font->font_size > 0.0f)
                    font.font_size = span.font->font_size;
            }
        }
    }

    // TICKET-101 follow-up — canonicalize the assembled FontSpec.
    chronon3d::text::internal::apply_fontspec_canonicalization(font);
    return font;
}

// ── split_by_font — divide a paragraph into font-homogeneous sub-ranges ───
//
// Two positions share a sub-range iff they observe the same span stack
// (and the resulting effective font is identical).  Boundaries are
// collected from span start/end markers inside `[para_start, para_end)`,
// sorted, deduped, then materialized into a list of FontSubRange entries.
[[nodiscard]] std::vector<FontSubRange> split_by_font(
    const TextDocument& doc,
    std::size_t         para_start,
    std::size_t         para_end
) {
    std::vector<FontSubRange> ranges;
    if (para_start >= para_end) return ranges;

    // Collect all span boundary points within the paragraph.
    std::vector<std::size_t> boundaries;
    boundaries.push_back(para_start);
    for (const auto& span : doc.spans) {
        if (span.byte_start > para_start && span.byte_start < para_end) {
            boundaries.push_back(span.byte_start);
        }
        if (span.byte_end > para_start && span.byte_end < para_end) {
            boundaries.push_back(span.byte_end);
        }
    }
    boundaries.push_back(para_end);

    // Sort + deduplicate.
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(
        std::unique(boundaries.begin(), boundaries.end()),
        boundaries.end());

    // Build font-homogeneous ranges.
    for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
        std::size_t start = boundaries[i];
        std::size_t end   = boundaries[i + 1];
        if (start >= end) continue;

        FontSpec font = effective_font(doc, start);
        ranges.push_back({start, end, std::move(font)});
    }

    return ranges;
}

} // namespace chronon3d::text::resolver

// ═══════════════════════════════════════════════════════════════════════════
// Back-compat: ResolvedTextRun emits the FontSubRange at the same address.
// Pre-M1.5#8 callers used an anonymous-namespace FontSubRange.  Now we
// forward-declare it in the orchestrator's TU.
// (No explicit export alias needed — split internals use the new type.)
// ═══════════════════════════════════════════════════════════════════════════
