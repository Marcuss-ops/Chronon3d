#include <chronon3d/text/animation/text_animator_stack.hpp>

#include <chronon3d/text/animation/glyph_instance_state.hpp>    // make_initial_glyph_states
#include <chronon3d/text/animation/text_animator_evaluator.hpp>  // evaluate_animator
#include <chronon3d/text/font_engine.hpp>                        // PlacedGlyphRun full type
#include <chronon3d/text/glyph_selector.hpp>                     // build_text_unit_map

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
    evaluate_animator_stack_into(states, animators, placed, source, time);
    return states;
}

// ═══════════════════════════════════════════════════════════════════════════
// evaluate_animator_stack_into — in-place stack evaluator (hot path)
// ════════════════════════════════════════════════════════════════════════
//
// Seed `inout_states` with `make_initial_glyph_states(placed)`, build the
// unit map once for the run, then evaluate each animator in order.  The
// vector is REPLACED — callers must not preserve the previous contents
// across calls.

void evaluate_animator_stack_into(
    std::vector<GlyphInstanceState>& inout_states,
    const std::vector<TextAnimatorSpec>& animators,
    const PlacedGlyphRun& placed,
    std::string_view source,
    SampleTime time
) {
    inout_states = make_initial_glyph_states(placed);
    auto unit_map = build_text_unit_map(placed, source);

    for (const auto& animator : animators) {
        evaluate_animator(animator, unit_map, inout_states, source, time, &placed);
    }
}

} // namespace chronon3d
