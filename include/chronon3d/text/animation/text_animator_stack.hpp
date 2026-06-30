#pragma once

#include <chronon3d/text/animation/text_animator_spec.hpp>      // TextAnimatorSpec
#include <chronon3d/text/animation/glyph_instance_state.hpp>    // GlyphInstanceState

#include <string_view>
#include <vector>

namespace chronon3d {

// Forward declarations — full types live in other headers.
struct PlacedGlyphRun;   // chronon3d/text/font_engine.hpp
struct TextUnitMap;      // chronon3d/text/glyph_selector.hpp

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
///
/// Builds the TextUnitMap internally via `build_text_unit_map` — acceptable
/// for one-shot callers; the per-frame hot path uses `_into` with a
/// pre-built unit map from `TextRunLayout::units`.
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
//
// `unit_map` must be a pre-built TextUnitMap for `placed` + `source`.
// The per-frame driver passes `TextRunLayout::units` (built once during
// layout) so the hot path avoids rebuilding it every frame.

void evaluate_animator_stack_into(
    std::vector<GlyphInstanceState>& inout_states,
    const std::vector<TextAnimatorSpec>& animators,
    const PlacedGlyphRun& placed,
    std::string_view source,
    const TextUnitMap& unit_map,
    SampleTime time
);

} // namespace chronon3d
