#pragma once

// ─── Chronon3D — GlyphSelectorSpec + selector enums (TICKET-046 Phase 1a) ──
//
// Extracted from glyph_selector.hpp so that consumers who only need
// GlyphSelectorSpec (e.g. text_span.hpp, text_animator_spec.hpp) can
// include this lightweight header WITHOUT pulling in the selector's
// GlyphSelectorUnitMap storage and builder API.
//
// glyph_selector.hpp includes this header transitively — existing
// callers are unaffected by the extraction.

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/core/types/types.hpp>

#include <cmath>
#include <string>
#include <optional>

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
    Smooth,     // cubic smooth ease curve (After Effects "Smooth")
    Wiggly,     // deterministic multiplicative per-unit modulation
    Noise       // deterministic coherent noise blended with the base weight
};

/// Range Selector units, matching After Effects' Units control.
/// Percentage keeps start/end/offset in 0..100.  Index interprets them as
/// logical unit indices, so `start=0, end=3` selects the first three units of
/// a run regardless of its length.
enum class TextSelectorRangeUnits : u8 {
    Percentage = 0,
    Index = 1,
};

/// Temporal interpolation of the Wiggly Selector noise lattice.
enum class WigglySelectorMode : u8 {
    Smooth = 0,  // smoothstep interpolation between lattice cells (AE Smooth)
    Corner = 1,  // linear interpolation between lattice cells (AE Corner)
    Jagged = 2,  // nearest lattice cell (AE Jagged)
};

/// Deterministic modulation used by Wiggly and Noise selector shapes.
/// `frequency` is measured in noise cycles per selected unit (0, 1000];
/// `amplitude` scales the modulation around the underlying shape weight and is
/// constrained to [0, 1].  The remaining fields mirror the After Effects
/// Wiggly Selector controls: temporal frequency, spatial correlation, phase
/// offsets, output bounds, dimension locking and lattice interpolation mode.
struct WigglySelectorParams {
    f32 frequency{1.0f};
    f32 amplitude{0.25f};
    u64 seed{0};

    // Wiggles/Second: temporal noise cycles per second. 0 freezes the noise in
    // time while retaining spatial variation.
    f32 wiggles_per_second{0.0f};
    // Correlation: 0..100 %. 100 makes every selected unit share the same
    // spatial sample (perfectly correlated neighbours); 0 leaves each unit
    // independent.
    f32 correlation{0.0f};
    // Phase offsets in degrees applied to the temporal and spatial noise axes.
    f32 temporal_phase{0.0f};
    f32 spatial_phase{0.0f};
    // Output bounds, in percent, applied after amplitude modulation.  The
    // defaults (0..100) leave the historical modulation unchanged.
    f32 min_amount{0.0f};
    f32 max_amount{100.0f};
    // AE Lock Dimensions.  Chronon resolves one scalar selector weight per
    // unit; when false the three implicit property dimensions use independent
    // noise channels and the scalar weight is their arithmetic mean.
    bool lock_dimensions{true};
    WigglySelectorMode mode{WigglySelectorMode::Smooth};
};

[[nodiscard]] inline bool valid_wiggly_selector_params(
    const WigglySelectorParams& params) noexcept {
    if (!std::isfinite(params.frequency) || params.frequency <= 0.0f ||
        params.frequency > 1000.0f)
        return false;
    if (!std::isfinite(params.amplitude) || params.amplitude < 0.0f ||
        params.amplitude > 1.0f)
        return false;
    if (!std::isfinite(params.wiggles_per_second) ||
        params.wiggles_per_second < 0.0f || params.wiggles_per_second > 1000.0f)
        return false;
    if (!std::isfinite(params.correlation) || params.correlation < 0.0f ||
        params.correlation > 100.0f)
        return false;
    if (!std::isfinite(params.temporal_phase) ||
        std::abs(params.temporal_phase) > 36000.0f ||
        !std::isfinite(params.spatial_phase) ||
        std::abs(params.spatial_phase) > 36000.0f)
        return false;
    if (!std::isfinite(params.min_amount) || params.min_amount < 0.0f ||
        params.min_amount > 100.0f || !std::isfinite(params.max_amount) ||
        params.max_amount < 0.0f || params.max_amount > 100.0f ||
        params.min_amount > params.max_amount)
        return false;
    switch (params.mode) {
        case WigglySelectorMode::Smooth:
        case WigglySelectorMode::Corner:
        case WigglySelectorMode::Jagged:
            break;
        default:
            return false;
    }
    return true;
}

// ── TextSelectorOrder — traversal direction ──────────────────────────────

enum class TextSelectorOrder {
    Forward,    // logical first → last (LTR left→right, RTL right→left)
    Reverse,    // logical last → first
    FromCenter, // logical centre outward (0 → ends)
    ToCenter,   // logical edges inward (ends → 0)
    Random,     // deterministic shuffle with seed
    Index       // ordered index bands; each step selects the next unit
};

// ── SelectorCombineMode — how multiple selectors blend ───────────────────

enum class SelectorCombineMode {
    Replace,   // overwrite previous weight
    Add,       // sum (clamped to [0,1])
    Subtract,  // subtract from previous
    Intersect, // min(previous, current)
    Min,        // min(previous, current) — alias for Intersect
    Max,        // max(previous, current)
    Difference  // absolute difference |previous - current|
};

// ── SelectorWeight — computed per-unit weight ────────────────────────────
// Range [0.0, 1.0]; 0.0 = property not applied, 1.0 = fully applied.

using SelectorWeight = f32;

/// Runtime selector binding shared by CPU preparation and GPU metadata upload.
/// Backends consume these parameters; selector-window and Smooth mathematics
/// remain owned by the canonical selector compiler.
struct SelectorWeightParams {
    TextSelectorShape shape{TextSelectorShape::Square};
    f32 start{0.0f};
    f32 end{100.0f};
    f32 offset{0.0f};
    f32 amount{100.0f};
    u32 total_units{0};
    u32 ordered_index{0};
    bool use_ordered_index{false};
    bool enabled{false};
    bool window_completed{false};
    f32 wiggly_frequency{1.0f};
    f32 wiggly_amplitude{0.25f};
    u64 wiggly_seed{0};

    // Range Selector Smoothness, in percent of the active range. 0 keeps the
    // historical hard shape; 100 feathers the whole range with smoothstep.
    f32 smoothness{0.0f};
    // Temporal binding for Wiggly and for expression selectors.
    f32 time_seconds{0.0f};
    f32 sample_frame{0.0f};
    f32 sample_fps{30.0f};
    f32 wiggles_per_second{0.0f};
    f32 correlation{0.0f};
    f32 temporal_phase{0.0f};
    f32 spatial_phase{0.0f};
    f32 min_amount{0.0f};
    f32 max_amount{100.0f};
    bool lock_dimensions{true};
    WigglySelectorMode wiggly_mode{WigglySelectorMode::Smooth};
    // Logical unit index used by AE expression selectors (`textIndex`).
    // `ordered_index` remains the traversal position for Forward/Reverse/etc.
    u32 raw_index{0};
};

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
    bool end_inclusive{false};                   // include character cells touching an AE range end
    bool randomize_order{false};                 // use deterministic shuffle
    u64  random_seed{0};                         // seed for deterministic shuffle
    WigglySelectorParams wiggly{};                // used by shape == Wiggly
    WigglySelectorParams noise{};                 // coherent modulation used by shape == Noise
    u32 index_step{1};                            // used only by order == Index
    std::optional<std::string> semantic_id;       // target one named semantic span

    // AE Range Selector Units: percentage (default) or logical index.
    TextSelectorRangeUnits range_units{TextSelectorRangeUnits::Percentage};
    // AE Range Selector Smoothness, 0..100 % of the active range.
    f32 smoothness{0.0f};
    // AE Expression Selector.  When present, the expression replaces the
    // selector weight.  Variables: textIndex (0-based logical unit index),
    // textTotal, selectorValue (0..100 base weight), time, frame, fps.
    // Empty strings are rejected by compile_selector().
    std::optional<std::string> expression;
};

} // namespace chronon3d
