#include <chronon3d/text/animation/text_animator_stack.hpp>

#include <chronon3d/text/animation/glyph_instance_state.hpp>    // reset_glyph_states_in_place
#include <chronon3d/text/animation/text_animator_evaluator.hpp>  // evaluate_animator
#include <chronon3d/text/font_engine.hpp>                        // PlacedGlyphRun full type
#include <chronon3d/text/glyph_selector.hpp>                     // build_text_unit_map, TextUnitMap

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// evaluate_animator_stack — public stack evaluator (allocates return)
// ═══════════════════════════════════════════════════════════════════════════
//
// Thin wrapper around `evaluate_animator_stack_into` that allocates and
// returns the resulting state vector. Equivalent in semantics; preferred
// by ad-hoc callers (tests, scripts, one-shot tooling).
//
// The per-frame animation driver in `src/text/text_run_driver.cpp` uses
// `evaluate_animator_stack_into` instead, so consecutive frames do not
// pay a fresh-vector allocation.

std::vector<GlyphInstanceState> evaluate_animator_stack(
    const std::vector<TextAnimatorSpec>& animators,
    const PlacedGlyphRun& placed,
    std::string_view source,
    SampleTime time
) {
    std::vector<GlyphInstanceState> states;
    auto unit_map = build_text_unit_map(placed, source);
    evaluate_animator_stack_into(states, animators, placed, source, unit_map, time);
    return states;
}

// ═══════════════════════════════════════════════════════════════════════════
// evaluate_animator_stack_into — in-place stack evaluator (hot path)
// ════════════════════════════════════════════════════════════════════════
//
// Reset `inout_states` with `reset_glyph_states_in_place(placed)`, which
// resizes only when the glyph count changes and otherwise mutates in place.
// Uses the caller-supplied `unit_map` (from TextRunLayout::units, built
// once during layout) — no per-frame TextUnitMap allocation.

void evaluate_animator_stack_into(
    std::vector<GlyphInstanceState>& inout_states,
    const std::vector<TextAnimatorSpec>& animators,
    const PlacedGlyphRun& placed,
    std::string_view source,
    const TextUnitMap& unit_map,
    SampleTime time
) {
    reset_glyph_states_in_place(inout_states, placed);

    for (const auto& animator : animators) {
        evaluate_animator(animator, unit_map, inout_states, source, time, &placed);
    }
}

} // namespace chronon3d
