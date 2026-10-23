#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_tracking_evaluator.hpp — INTERNAL header (src/-only)
// ═══════════════════════════════════════════════════════════════════════════
//
// Declares the cumulative-tracking applier.  TrackingProperty is the only
// property type that needs to share state ACROSS consecutive glyphs in a
// stack — After Effects-style "letter-spacing gap between consecutive
// letters" semantics.  This header therefore threads a
// `f32& cumulative_tracking_delta` accumulator by reference; the caller
// retains it for the lifetime of one animator's per-glyph loop.
//
// Why a separate file:
//   - The accumulator's lifecycle (declared in the evaluator, passed by
//     reference) is structurally distinct from the per-property dispatch
//     in `text_property_applier.cpp`.
//   - Keeping it isolated prevents accidental copy-paste of the
//     "apply-first, then increment" semantics into the dispatcher.
//
// Not part of the public SDK API.  Lives in `src/text/animation/`.

#include <chronon3d/text/animation/glyph_instance_state.hpp>
#include <chronon3d/text/animation/text_animator_properties.hpp>  // TrackingProperty
#include <chronon3d/text/glyph_selector_spec.hpp>              // SelectorWeight (TICKET-046 Phase 1a)
#include <chronon3d/core/types/sample_time.hpp>                     // SampleTime

namespace chronon3d::detail {

/// Apply TrackingProperty to a glyph using the running-sum accumulator.
///
/// Algorithm (apply-first, then increment), port from V1
/// `include/chronon3d/text/text_animator.hpp:325-337`:
///
///     cumulative_tracking_delta = 0;
///     for each glyph gi:
///         gs[gi].position.x += cumulative_tracking_delta;  // apply CURRENT sum
///         cumulative_tracking_delta += tracking_for_gi;     // increment NEXT
///
/// So the FIRST glyph receives cumulative = 0 (no preceding letter to
/// spread from), and subsequent glyphs each receive the running sum
/// accumulated from earlier iterations. This is the AE-style semantic.
///
/// @param gs                            Glyph state to mutate (position.x).
/// @param prop                          TrackingProperty carrying the
///                                      per-frame tracking value.
/// @param weight                        Selector weight for this glyph.
/// @param blend                         Blend mode for the active side
///                                      (transform vs color) — Tracking uses
///                                      transform.
/// @param time                          Frame sample time.
/// @param cumulative_tracking_delta     In/out running sum, owned by the
///                                      evaluator loop. Updated in place.
void apply_tracking_to_glyph(
    GlyphInstanceState& gs,
    const TrackingProperty& prop,
    SelectorWeight weight,
    TextPropertyBlendMode blend,
    SampleTime time,
    f32& cumulative_tracking_delta
);

} // namespace chronon3d::detail
