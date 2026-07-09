#pragma once

// ─── Chronon3D — GlyphSelectorSpec + selector enums (TICKET-046 Phase 1a) ──
//
// Extracted from glyph_selector.hpp so that consumers who only need
// GlyphSelectorSpec (e.g. text_span.hpp, text_animator_spec.hpp) can
// include this lightweight header WITHOUT pulling in the narrow
// struct TextUnitMap (which would cause an ODR conflict with the
// canonical 8-level class TextUnitMap in text_unit_map.hpp).
//
// glyph_selector.hpp includes this header transitively — existing
// callers are unaffected by the extraction.

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/types.hpp>

#include <string>

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

} // namespace chronon3d
