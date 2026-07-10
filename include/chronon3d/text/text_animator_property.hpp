#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_animator_property.hpp — COMPATIBILITY header
// ═══════════════════════════════════════════════════════════════════════════
//
// This header is preserved as a thin re-include shim so that existing
// consumers — every `#include <chronon3d/text/text_animator_property.hpp>`
// in the project — continue to compile unchanged after the Refactor 2
// split of `src/text/text_animator_property.cpp` and  // drift-allow: stale-ref
// `include/chronon3d/text/text_animator_property.hpp` into the
// `text/animation/` subdirectory.
//
// The full set of symbols that this shim re-exports is split across the
// canonical headers below. New code SHOULD include those headers
// directly (smaller per-TU include surface):
//
//   Glyph / state
//     <chronon3d/text/animation/glyph_instance_state.hpp>
//     <chronon3d/text/animation/text_animator_properties.hpp>
//   Spec / stack
//     <chronon3d/text/animation/text_animator_spec.hpp>
//     <chronon3d/text/animation/text_animator_stack.hpp>
//   Evaluator
//     <chronon3d/text/animation/text_animator_evaluator.hpp>
//
// The implementation moved from a single god-file to five focused TU's
// under `src/text/animation/` (each ≤150 LOC except the canonical
// property dispatcher), with `text_property_stage.hpp` and
// `text_tracking_evaluator.hpp` carrying internal-only declarations.

#include <chronon3d/text/animation/text_animator_properties.hpp>  // enums, structs, variant
#include <chronon3d/text/animation/text_animator_spec.hpp>        // TextAnimatorSpec, TextAnimatorStack
#include <chronon3d/text/animation/glyph_instance_state.hpp>      // GlyphInstanceState, make_initial_glyph_states
#include <chronon3d/text/animation/text_animator_evaluator.hpp>   // evaluate_animator
#include <chronon3d/text/animation/text_animator_stack.hpp>       // evaluate_animator_stack[_into]
