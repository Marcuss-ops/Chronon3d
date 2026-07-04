// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// src/text/resolver/text_span_resolver.hpp — INTERNAL
//
// M1.5#8 — public surface declaration of the span resolver module.
// STRICTLY INTERNAL: lives under src/text/resolver/, NOT in
// include/chronon3d/, NOT installed with the SDK (Cat-3 freeze
// compliance — no new public SDK headers).
//
// Consumers in M1.5#8:
//   * text_run_resolver.cpp — calls split_by_font (orchestrator).
//   * text_bidi_resolver.cpp — uses sub.font.font_family for FontRequest
//     propagation (falls through FontResolver).

#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_document.hpp>

#include <cstddef>
#include <vector>

namespace chronon3d::text::resolver {

// ── FontSubRange — paragraph-local byte range with resolved FontSpec ──────
//
// M1.5#8 — pulled out of text_resolver.cpp's anonymous namespace into a
// named type so other split files (text_bidi_resolver, text_run_resolver)
// can use it as a parameter without depending on text_resolver.cpp.
struct FontSubRange {
    std::size_t byte_start{0};
    std::size_t byte_end{0};
    FontSpec    font;
};

// Split a paragraph's byte range into font-homogeneous sub-ranges based
// on the stacking of TextStyleSpan overrides on top of doc.defaults.font.
// Two positions share a sub-range iff they observe the same span stack
// (and the effective font is identical).  Returns an empty vector for
// empty paragraph ranges.
[[nodiscard]] std::vector<FontSubRange> split_by_font(
    const TextDocument& doc,
    std::size_t         para_start,
    std::size_t         para_end
);

} // namespace chronon3d::text::resolver
