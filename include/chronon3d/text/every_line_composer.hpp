#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// every_line_composer.hpp — Knuth-Plass simplified global paragraph composer
//
// PR 3 of the text pipeline.  Uses dynamic programming to find the globally
// optimal set of line breaks for a paragraph, minimising badness (ratio³)
// plus penalty terms for hyphenation, widows, and orphans.
//
// The public entry point is compose_single_line_paragraph() in
// single_line_composer.hpp — it dispatches to this algorithm when
// ParagraphStyle::composer == ParagraphComposer::EveryLine.
//
// Complexity: O(n²) where n = number of break opportunities.
// Best for: static title cards, pre-rendered text, print-quality layout.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/composer_types.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/paragraph_style.hpp>

#include <string_view>
#include <vector>

namespace chronon3d {

// Forward-declared; implemented in every_line_composer.cpp.
// Called by compose_single_line_paragraph() when composer==EveryLine.

namespace composer_internal {

[[nodiscard]] ParagraphLayout compose_every_line_impl(
    const std::vector<ShapedCluster>& clusters,
    float available_width,
    const ParagraphStyle& style,
    std::string_view source_text,
    const PlacedGlyphRun& shaped
);

} // namespace composer_internal

} // namespace chronon3d
