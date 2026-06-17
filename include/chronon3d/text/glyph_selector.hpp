#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace chronon3d {

// ── TextSelectorUnit — granularity of selection ──────────────────────────

enum class TextSelectorUnit {
    Glyph,      // individual glyph (from HarfBuzz shaping)
    Grapheme,   // user-perceived character (grapheme cluster)
    Character,  // code-point boundary (legacy)
    Word,       // whitespace-delimited word
    Line        // newline-delimited line
};

// ── TextSelectorShape — how the weight falls off across the range ────────

enum class TextSelectorShape {
    Square,     // 1.0 inside [start, end], 0.0 outside
    RampUp,     // linear ramp 0→1 from start to end
    RampDown,   // linear ramp 1→0 from start to end
    Triangle,   // linear ramp up then down, peak at midpoint
    Round,      // smoothstep-like curve
    Smooth      // cubic smooth ease curve (After Effects "Smooth")
};

// ── TextSelectorOrder — traversal direction ──────────────────────────────

enum class TextSelectorOrder {
    Forward,    // left → right (or first → last in RTL)
    Reverse,    // right → left
    FromCenter, // centre outward (0 → ends)
    ToCenter,   // edges inward (ends → 0)
    Random      // deterministic shuffle with seed
};

// ── SelectorCombineMode — how multiple selectors blend ───────────────────

enum class SelectorCombineMode {
    Replace,   // overwrite previous weight
    Add,       // sum (clamped to [0,1])
    Subtract,  // subtract from previous
    Intersect, // min(previous, current)
    Min,       // min(previous, current) — alias for Intersect
    Max        // max(previous, current)
};

// ── SelectorWeight — computed per-unit weight ────────────────────────────
// Range [0.0, 1.0]; 0.0 = property not applied, 1.0 = fully applied.

using SelectorWeight = f32;

// ── GlyphSelectorSpec — declarative selector description ─────────────────
//
// Each selector computes a weight ∈ [0,1] for every glyph unit.
// start/end/offset/amount are AnimatedValue<f32> so they can use keyframes,
// easing curves, and expressions already present in the engine.

struct GlyphSelectorSpec {
    std::string id;                              // unique id for diagnostics

    TextSelectorUnit    unit{TextSelectorUnit::Glyph};
    TextSelectorShape   shape{TextSelectorShape::Smooth};
    TextSelectorOrder   order{TextSelectorOrder::Forward};
    SelectorCombineMode combine{SelectorCombineMode::Replace};

    AnimatedValue<f32> start{0.0f};              // 0–100 normalised
    AnimatedValue<f32> end{100.0f};              // 0–100 normalised
    AnimatedValue<f32> offset{0.0f};             // 0–100 normalised, wraps
    AnimatedValue<f32> amount{100.0f};           // 0–100 %, multiplies final weight

    f32 ease_low{0.0f};                          // 0–100 % of range for ease-in
    f32 ease_high{100.0f};                       // 0–100 % of range for ease-out

    bool exclude_spaces{true};                   // skip whitespace-only units
    bool randomize_order{false};                 // use deterministic shuffle
    u64  random_seed{0};                         // seed for deterministic shuffle
};

// ── TextUnitMap — immutable index map from glyph → unit ──────────────────
//
// Built once per PlacedGlyphRun + source text. Maps every glyph index to
// the 0-based index of its grapheme, word, and line.  The counts let the
// evaluator normalise unit indices to 0..100.

struct TextUnitMap {
    std::vector<u32> glyph_to_grapheme;   // glyph_index → grapheme_unit_index
    std::vector<u32> glyph_to_word;       // glyph_index → word_unit_index
    std::vector<u32> glyph_to_line;       // glyph_index → line_unit_index

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
    SampleTime time
);

/// Evaluate a stack of selectors with combine modes.
/// Evaluates each selector in order and combines their weights.
/// Returns the final combined weight in [0.0, 1.0].
[[nodiscard]] SelectorWeight evaluate_selectors(
    const std::vector<GlyphSelectorSpec>& specs,
    const TextUnitMap& unit_map,
    u32 glyph_index,
    std::string_view source,
    SampleTime time
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

} // namespace detail

} // namespace chronon3d
