#pragma once

#include <chronon3d/text/animation/text_animator_spec.hpp>      // TextAnimatorSpec
#include <chronon3d/text/animation/glyph_instance_state.hpp>    // GlyphInstanceState
#include <chronon3d/text/glyph_selector.hpp>                    // TextUnitMap

#include <string_view>
#include <vector>

namespace chronon3d {

// Forward declaration — full type lives in chronon3d/text/font_engine.hpp.
struct PlacedGlyphRun;

// ═══════════════════════════════════════════════════════════════════════════
// evaluate_animator — single-spec evaluator entry point
// ═══════════════════════════════════════════════════════════════════════════
//
// Evaluate a single animator spec for all glyphs in the given glyph-state
// vector, mutating them in place. Implementation in
// `src/text/animation/text_animator_evaluator.cpp`.
//
// Per-glyph pipeline:
//   1. Compute selector weight (sum of `spec.selectors` weights).
//   2. For each property in `spec.properties`:
//      a. TrackingProperty → cumulative running-sum accumulator
//         (`detail::apply_tracking_to_glyph` in
//         src/text/animation/text_tracking_evaluator.cpp).
//      b. Else → single canonical dispatcher
//         (`detail::apply_property_to_glyph` in
//         src/text/animation/text_property_applier.cpp`) which `std::visit`s
//         the TextAnimatorProperty variant — no per-property evaluator split.
//
// `placed`: optional pointer used by words/lines/glyphs-with-cluster
// selectors that need to inspect the resolved cluster map to honour
// `exclude_spaces`. Pass `nullptr` (the default) when the selector does
/// not need cluster info.

void evaluate_animator(
    const TextAnimatorSpec& spec,
    const TextUnitMap& unit_map,
    std::vector<GlyphInstanceState>& glyph_states,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed = nullptr
);

} // namespace chronon3d
