#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_property_stage.hpp — INTERNAL header (src/-only)
// ═══════════════════════════════════════════════════════════════════════════
//
// Declares the single canonical per-property dispatcher used by the
// animator evaluator.  Lives in `src/text/animation/` (NOT under
// `include/chronon3d/`) because `detail::apply_property_to_glyph` is NOT
// part of the public SDK API.
//
// IMPORTANT: this is the only place that decides WHAT each property type
// does to a glyph. There is deliberately NO per-property evaluator file
// (no position_evaluator.cpp, no opacity_evaluator.cpp, etc.). New
// property types are added here, in the variant (text_animator_properties.hpp),
// and in the property struct — that's it.
//
// Consumers:
//   - src/text/animation/text_animator_evaluator.cpp (calls
//     detail::apply_property_to_glyph for non-tracking properties).
//
// Not used by:
//   - src/text/animation/text_tracking_evaluator.cpp (TrackingProperty is
//     handled separately because of its cumulative across-glyph accumulator)
//   - src/text/animation/text_animator_stack.cpp (only wires the loops).

#include <chronon3d/text/animation/glyph_instance_state.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>  // TextAnimatorProperty, TextPropertyBlendMode
#include <chronon3d/text/glyph_selector.hpp>                       // SelectorWeight
#include <chronon3d/core/types/sample_time.hpp>                     // SampleTime

namespace chronon3d::detail {

/// Apply a single property to a glyph state, scaled by weight.
///
/// Implements the single canonical dispatch.  The function dispatches on
/// `prop` via `std::visit + if constexpr`, with one branch per variant
/// alternative.  TrackingProperty is a no-op here on purpose — the
/// cumulative across-glyph accumulator that requires per-iteration state
/// lives in `detail::apply_tracking_to_glyph`
/// (text_tracking_evaluator.hpp/cpp) and is invoked by the evaluator
/// before this dispatcher is called for non-tracking properties.
///
/// @param gs    Glyph state to mutate.
/// @param prop  Property to apply (decides which branch runs).
/// @param weight Selector weight ∈ [0, 1].  Weights ≤ 0 short-circuit.
/// @param blend Blend mode for the active side (transform vs color mode
///              chosen by the evaluator).
/// @param time  Frame sample time — used by AnimatedValue-backed properties.
void apply_property_to_glyph(
    GlyphInstanceState& gs,
    const TextAnimatorProperty& prop,
    SelectorWeight weight,
    TextPropertyBlendMode blend,
    SampleTime time
);

} // namespace chronon3d::detail
