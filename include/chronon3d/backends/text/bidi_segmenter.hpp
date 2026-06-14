#pragma once

// ── Bidi Segmenter ──────────────────────────────────────────────────────────
//
// Real Unicode bidirectionality support using the FriBidi library.
// Segments a UTF-8 text string into directional runs (LTR / RTL) by applying
// the Unicode Bidirectional Algorithm (UAX #9).  Each run carries the resolved
// direction that should be forwarded to HarfBuzz during shaping, enabling
// correct glyph substitution and positioning for mixed LTR+RTL text (e.g.
// Arabic + English, Hebrew + English).
//
// Usage (in text_layout_engine or rasterizer):
//   std::string mixed = "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 World";
//   auto runs = segment_bidi_runs(mixed);
//   // → [{text="Hello ", dir=LTR}, {text="\xD9\x85...", dir=RTL}, {text=" World", dir=LTR}]
//
// Thread-safety: All functions are re-entrant (no mutable global state).

#include <string>
#include <string_view>
#include <vector>

#include <chronon3d/text/font_engine.hpp> // TextDirection

namespace chronon3d {

/// One directional run produced by the bidi segmenter.
/// The `text` is a contiguous substring of the original input that has a
/// uniform resolved direction.  Shape it with `direction` set explicitly
/// on the TextShaping struct passed to FontEngine::shape_text().
struct BidiRun {
    std::string text;           // The text for this run (UTF-8)
    TextDirection direction;    // LTR or RTL (never Auto)
    size_t byte_offset{0};      // Byte offset into the original text
};

/// Segment a UTF-8 string into directional runs using FriBidi's
/// implementation of the Unicode Bidirectional Algorithm (UAX #9).
///
/// @param text      UTF-8 input text (may contain mixed LTR/RTL)
/// @param base_dir  Requested paragraph base direction.  Pass
///                  FRIBIDI_PAR_DIR_LTR or FRIBIDI_PAR_DIR_RTL to force a
///                  particular base direction, or FRIBIDI_PAR_DIR_AUTO (0)
///                  to auto-detect from the first strong character.
/// @return A vector of BidiRun in LOGICAL order, each with its resolved
///         direction.  Empty if the input is empty or an error occurs.
[[nodiscard]] std::vector<BidiRun> segment_bidi_runs(
    std::string_view text,
    int base_dir = 0  // 0 = auto, 1 = LTR, 2 = RTL (FriBidiParType)
);

} // namespace chronon3d
