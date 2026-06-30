#pragma once

#include <chronon3d/text/animation/text_animator_spec.hpp>      // TextAnimatorSpec
#include <chronon3d/text/animation/glyph_instance_state.hpp>    // GlyphInstanceState

#include <string_view>
#include <vector>

namespace chronon3d {

// Forward declaration — full type lives in chronon3d/text/font_engine.hpp.
struct PlacedGlyphRun;

// ═══════════════════════════════════════════════════════════════════════════
// evaluate_animator_stack — public stack evaluator (allocates return)
// ═══════════════════════════════════════════════════════════════════════════
//
// Evaluate a full animator stack for all glyphs.  Processes animators in
/// order, applying blend modes per spec.  Returns final glyph states.
///
/// Implementation in `src/text/animation/text_animator_stack.cpp`.
/// This is the per-frame driver used by ad-hoc callers (tests, scripts).
/// Hot paths (per-frame animation driver in `src/text/text_run_driver.cpp`)
/// use `evaluate_animator_stack_into` to avoid the per-call allocation.
[[nodiscard]] std::vector<GlyphInstanceState> evaluate_animator_stack(
    const std::vector<TextAnimatorSpec>& animators,
    const PlacedGlyphRun& placed,
    std::string_view source,
    SampleTime time
);

// ═══════════════════════════════════════════════════════════════════════════
// evaluate_animator_stack_into — in-place stack evaluator (driver path)
// ═══════════════════════════════════════════════════════════════════════════
//
// Re-evaluate the full animator stack, writing per-glyph state back into
// the provided `inout_states` vector. Semantically equivalent to
// `evaluate_animator_stack` but allocates no return value — used by the
// per-frame animation driver so consecutive frames don't allocate a new
/// vector.
//
// The vector is RESET in place via `reset_glyph_states_in_place`, which
// resizes only when `placed.glyphs.size()` changes and otherwise mutates
// each element in place.  Callers must NOT preserve the previous contents
// across calls.

void evaluate_animator_stack_into(
    std::vector<GlyphInstanceState>& inout_states,
    const std::vector<TextAnimatorSpec>& animators,
    const PlacedGlyphRun& placed,
    std::string_view source,
    SampleTime time
);

} // namespace chronon3d
