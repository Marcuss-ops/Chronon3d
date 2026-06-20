#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// single_line_composer.hpp — Greedy single-line-at-a-time paragraph composer
//
// PR 2 of the text pipeline.  Consumes already-shaped PlacedGlyphRun clusters,
// breaks them into lines using a greedy algorithm (SingleLine composer), and
// applies indentation + justification + hanging punctuation.
//
// Usage:
//   1. Shape text with FontEngine + resolve_placed_glyph_run()
//   2. Call compose_single_line_paragraph(shaped, box_width, style)
//   3. The resulting ParagraphLayout feeds into TextRunLayout final assembly.
//
// Complexity: O(n) where n = number of clusters.
// Best for: subtitles, lower-thirds, headlines, real-time text updates.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/composer_types.hpp>
#include <chronon3d/text/font_engine.hpp>       // PlacedGlyphRun
#include <chronon3d/text/paragraph_style.hpp>

#include <string_view>

namespace chronon3d {

/// Compose a single paragraph using the greedy SingleLine algorithm.
///
/// @param shaped    Already-shaped glyph run (from HarfBuzz + resolve_placed_glyph_run).
///                  Must have clusters populated.
/// @param box_width Available horizontal space in pixels (before indentation is applied).
/// @param style     Paragraph-level typography settings (indentation, justification,
///                  hanging punctuation, composer).
/// @param source_text The original UTF-8 text.  REQUIRED for correct whitespace/punctuation
///                  detection and line breaking.  Must span the same text that was shaped.
/// @return A ParagraphLayout containing the composed lines.
///
/// Behaviour by composer mode:
///   - SingleLine (default): greedy O(n) line breaking.
///   - EveryLine: Knuth-Plass simplified global optimisation O(n²).
///     Dispatched automatically — no separate call needed.
[[nodiscard]] ParagraphLayout compose_single_line_paragraph(
    const PlacedGlyphRun& shaped,
    float box_width,
    const ParagraphStyle& style,
    std::string_view source_text
);

} // namespace chronon3d
