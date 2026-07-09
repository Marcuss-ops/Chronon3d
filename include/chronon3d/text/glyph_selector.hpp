#pragma once

// ── TICKET-046 Phase 1a — selector enums + GlyphSelectorSpec extracted to
// glyph_selector_spec.hpp so consumers that only need selector types can
// include that header without pulling in the narrow struct TextUnitMap.

#include <chronon3d/text/glyph_selector_spec.hpp>

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace chronon3d {

// ── TextUnitMap — immutable index map from glyph → unit ──────────────────
//
// Built once per PlacedGlyphRun + source text. Maps every glyph index to
// the 0-based index of its grapheme, word, and line.  The counts let the
// evaluator normalise unit indices to 0..100.

struct TextUnitMap {
    std::vector<u32> glyph_to_grapheme;   // glyph_index → grapheme_unit_index
    std::vector<u32> glyph_to_word;       // glyph_index → word_unit_index
    std::vector<u32> glyph_to_line;       // glyph_index → line_unit_index

    /// Precomputed first glyph for each word/line unit — enables O(1)
    /// whitespace exclusion queries in CompiledSelector.
    std::vector<u32> first_glyph_for_word;  // word_idx → first glyph index
    std::vector<u32> first_glyph_for_line;  // line_idx → first glyph index

    u32 grapheme_count{0};
    u32 word_count{0};
    u32 line_count{0};

    /// Return the unit index for a given glyph based on the selector unit type.
    [[nodiscard]] u32 unit_index(u32 glyph_index, TextSelectorUnit unit) const noexcept {
        switch (unit) {
            case TextSelectorUnit::Glyph:     return glyph_index;
            case TextSelectorUnit::Grapheme:  return glyph_to_grapheme[glyph_index];
            case TextSelectorUnit::Character: return glyph_to_grapheme[glyph_index]; // same for now
            case TextSelectorUnit::Word:      return glyph_to_word[glyph_index];
            case TextSelectorUnit::Line:      return glyph_to_line[glyph_index];
        }
        return glyph_index;
    }

    /// Return the total number of units for the given unit type.
    [[nodiscard]] u32 unit_count(TextSelectorUnit unit) const noexcept {
        switch (unit) {
            case TextSelectorUnit::Glyph:     return static_cast<u32>(glyph_to_grapheme.size());
            case TextSelectorUnit::Grapheme:  return grapheme_count;
            case TextSelectorUnit::Character: return grapheme_count;
            case TextSelectorUnit::Word:      return word_count;
            case TextSelectorUnit::Line:      return line_count;
        }
        return static_cast<u32>(glyph_to_grapheme.size());
    }
};

// ── CompiledSelector — precomputed selector ready for per-glyph evaluation ─
//
// Built once per (GlyphSelectorSpec + TextUnitMap + source + placed) tuple.
// Precomputes everything the per-glyph hot path would otherwise recompute:
//   • whitespace-exclusion bitset per unit (O(1) instead of O(G) scan)
//   • Fisher-Yates permutation for Random order (owned, no thread_local cache)
//   • cached unit count
//
// The CompiledSelector owns its precomputed data and stores a non-owning
// pointer to the original GlyphSelectorSpec.  The caller must keep the spec
// alive for the lifetime of the CompiledSelector.

struct CompiledSelector {
    const GlyphSelectorSpec* spec{nullptr};  // non-owning (must outlive this)

    u32 total_units{0};

    /// Precomputed whitespace exclusion: unit_is_whitespace[i] == true
    /// means unit i should be excluded when exclude_spaces is active.
    std::vector<bool> unit_is_whitespace;

    /// Precomputed Fisher-Yates permutation for Random order.
    std::vector<u32> random_permutation;
};

/// Compile a single selector spec into a CompiledSelector.
[[nodiscard]] CompiledSelector compile_selector(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    std::string_view source,
    const PlacedGlyphRun* placed = nullptr
);

/// Compile a vector of selector specs.
[[nodiscard]] std::vector<CompiledSelector> compile_selectors(
    const std::vector<GlyphSelectorSpec>& specs,
    const TextUnitMap& unit_map,
    std::string_view source,
    const PlacedGlyphRun* placed = nullptr
);

/// Evaluate a pre-compiled selector for a single glyph (O(1)).
[[nodiscard]] SelectorWeight evaluate_compiled_selector(
    const CompiledSelector& compiled,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    SampleTime time
);

/// Evaluate a stack of pre-compiled selectors with combine modes.
[[nodiscard]] SelectorWeight evaluate_compiled_selectors(
    const std::vector<CompiledSelector>& compiled,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    SampleTime time
);

// ── Build TextUnitMap from a PlacedGlyphRun + source text ────────────────

/// Builds the glyph→grapheme, glyph→word, and glyph→line index maps.
/// Uses cluster info and source text to identify word and line boundaries.
/// @param placed   The fully-resolved placed glyph run (from FontEngine).
/// @param source   The original source text (UTF-8).
[[nodiscard]] TextUnitMap build_text_unit_map(
    const PlacedGlyphRun& placed,
    std::string_view source
);

// ── Selector evaluation ──────────────────────────────────────────────────

/// Evaluate a single selector for a single glyph at a given time.
/// Returns a weight in [0.0, 1.0].
///
/// @param spec       The selector specification.
/// @param unit_map   The pre-built unit index map.
/// @param glyph_index Index of the glyph within the PlacedGlyphRun.
/// @param source     The original source text (for space detection).
/// @param time       The sample time for AnimatedValue evaluation.
[[nodiscard]] SelectorWeight evaluate_selector(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed = nullptr
);

/// Evaluate a stack of selectors with combine modes.
/// Evaluates each selector in order and combines their weights.
/// Returns the final combined weight in [0.0, 1.0].
[[nodiscard]] SelectorWeight evaluate_selectors(
    const std::vector<GlyphSelectorSpec>& specs,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed = nullptr
);

// ── Internal helpers (exposed for testing) ───────────────────────────────

namespace detail {

/// Evaluate the shape function for a given unit position.
/// unit_position ∈ [0, 1] is the normalised position of the unit.
/// start, end ∈ [0, 1] define the active window.
[[nodiscard]] f32 evaluate_selector_shape(
    TextSelectorShape shape,
    f32 unit_position,
    f32 start,
    f32 end
);

/// Reorder a unit index based on the selector order.
/// total_units must be > 0.
[[nodiscard]] u32 apply_selector_order(
    TextSelectorOrder order,
    u32 original_index,
    u32 total_units,
    u64 random_seed
);

/// Deterministic hash-based unit float in [0, 1).
/// Uses the same key every time for the same (seed, unit_index) pair.
[[nodiscard]] f32 hash_to_unit_float(u64 seed, u64 unit_index);

/// Returns true if `cp` is a Unicode whitespace or separator character.
/// Covers ASCII (space, tab, CR, LF) plus common Unicode whitespace:
/// U+00A0 NBSP, U+1680 ogham space, U+2000–U+200A (en/em/thin/hair spaces),
/// U+2028 LINE SEPARATOR, U+2029 PARAGRAPH SEPARATOR,
/// U+202F NARROW NO-BREAK SPACE, U+205F MEDIUM MATHEMATICAL SPACE,
/// U+3000 IDEOGRAPHIC SPACE, U+FEFF ZERO WIDTH NO-BREAK SPACE.
[[nodiscard]] inline bool is_unicode_whitespace(char32_t cp) noexcept {
    if (cp <= 0x7F) {
        return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r';
    }
    switch (cp) {
        case 0x00A0:  // NO-BREAK SPACE
        case 0x1680:  // OGHAM SPACE MARK
        case 0x2000:  // EN QUAD
        case 0x2001:  // EM QUAD
        case 0x2002:  // EN SPACE
        case 0x2003:  // EM SPACE
        case 0x2004:  // THREE-PER-EM SPACE
        case 0x2005:  // FOUR-PER-EM SPACE
        case 0x2006:  // SIX-PER-EM SPACE
        case 0x2007:  // FIGURE SPACE
        case 0x2008:  // PUNCTUATION SPACE
        case 0x2009:  // THIN SPACE
        case 0x200A:  // HAIR SPACE
        case 0x2028:  // LINE SEPARATOR
        case 0x2029:  // PARAGRAPH SEPARATOR
        case 0x202F:  // NARROW NO-BREAK SPACE
        case 0x205F:  // MEDIUM MATHEMATICAL SPACE
        case 0x3000:  // IDEOGRAPHIC SPACE
        case 0xFEFF:  // ZERO WIDTH NO-BREAK SPACE / BOM
            return true;
        default:
            return false;
    }
}

/// Decode a single UTF-8 code point starting at `source[start]`.
/// On success, sets `cp_len` to the byte length (1–4) and returns the
/// code point.  On decode error (truncated, overlong, surrogate), returns
/// U+FFFD REPLACEMENT CHARACTER and sets cp_len to 1.
[[nodiscard]] inline char32_t decode_utf8_codepoint_from(
    std::string_view source,
    size_t start,
    size_t& cp_len
) noexcept {
    if (start >= source.size()) {
        cp_len = 0;
        return 0xFFFD;
    }
    const unsigned char lead = static_cast<unsigned char>(source[start]);
    if ((lead & 0x80u) == 0) {
        cp_len = 1;
        return static_cast<char32_t>(lead);
    }
    if ((lead & 0xE0u) == 0xC0u) {
        cp_len = 2;
        if (start + 1 >= source.size()) return 0xFFFD;
        return static_cast<char32_t>(
            ((lead & 0x1Fu) << 6) |
            (static_cast<unsigned char>(source[start + 1]) & 0x3Fu));
    }
    if ((lead & 0xF0u) == 0xE0u) {
        cp_len = 3;
        if (start + 2 >= source.size()) return 0xFFFD;
        return static_cast<char32_t>(
            ((lead & 0x0Fu) << 12) |
            ((static_cast<unsigned char>(source[start + 1]) & 0x3Fu) << 6) |
            (static_cast<unsigned char>(source[start + 2]) & 0x3Fu));
    }
    if ((lead & 0xF8u) == 0xF0u) {
        cp_len = 4;
        if (start + 3 >= source.size()) return 0xFFFD;
        return static_cast<char32_t>(
            ((lead & 0x07u) << 18) |
            ((static_cast<unsigned char>(source[start + 1]) & 0x3Fu) << 12) |
            ((static_cast<unsigned char>(source[start + 2]) & 0x3Fu) << 6) |
            (static_cast<unsigned char>(source[start + 3]) & 0x3Fu));
    }
    cp_len = 1;
    return 0xFFFD;
}

/// Returns true if all code points in [start, start+len) are Unicode
/// whitespace.  Uses `is_unicode_whitespace` for code-point-level checks.
/// Empty range or slice past `source.size()` is treated as whitespace
/// (conservative: nothing-of-interest).
[[nodiscard]] inline bool is_whitespace_run(
    std::string_view source,
    size_t start,
    size_t len
) noexcept {
    if (len == 0) return true;
    if (start >= source.size()) return true;
    const size_t end = std::min(start + len, source.size());
    size_t pos = start;
    while (pos < end) {
        size_t cp_len = 0;
        const char32_t cp = detail::decode_utf8_codepoint_from(source, pos, cp_len);
        if (cp_len == 0) break;
        if (!detail::is_unicode_whitespace(cp)) return false;
        pos += cp_len;
    }
    return true;
}

/// Returns true if the unit containing `glyph_index` is whitespace-only and
/// should be excluded when `spec.exclude_spaces` is true. Requires `placed`
/// for cluster byte-offset lookup at the per-glyph granularity. For word /
/// line units, walks TextUnitMap to find the first glyph belonging to that
/// unit and inspects its cluster's byte range.
///
/// When `placed == nullptr`, this returns false (caller must opt in by
/// threading the placed run through). This preserves backward compatibility
/// for API consumers that don't pass it.
[[nodiscard]] bool should_exclude_unit(
    const GlyphSelectorSpec& spec,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    const PlacedGlyphRun* placed
);

/// Returns a deterministic permutation of [0..total_units) seeded by `seed`,
/// precomputed via Fisher-Yates and cached per-thread per (seed, total).
/// The returned vector has length `total_units`; entry `perm[i]` is the
/// output position where the original index `i` lands after the shuffle.
/// This guarantees a bijection — every output position is hit exactly once,
/// unlike the previous `floor(rand * total)` scheme which could collide.
[[nodiscard]] const std::vector<u32>& get_or_build_permutation(
    u64 seed,
    u32 total_units
);

} // namespace detail

} // namespace chronon3d
