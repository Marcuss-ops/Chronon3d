#include "text_tracking_evaluator.hpp"

namespace chronon3d::detail {

// ═══════════════════════════════════════════════════════════════════════════
// apply_tracking_to_glyph — cumulative tracking applier
// ═══════════════════════════════════════════════════════════════════════════
//
// Apply-first-then-increment semantics, port from V1
// `include/chronon3d/text/text_animator.hpp:325-337`. The
// `cumulative_tracking_delta` parameter is owned by the evaluator loop
// (`text_animator_evaluator.cpp`) and is shared across consecutive
// glyphs within one animator; this function reads it BEFORE applying
// the per-glyph delta and writes it back AFTER incrementing. That
// ordering is what produces AE-style "letter-spacing gap between
// consecutive letters" output (the first glyph starts at 0).

void apply_tracking_to_glyph(
    GlyphInstanceState& gs,
    const TrackingProperty& prop,
    SelectorWeight weight,
    TextPropertyBlendMode blend,
    SampleTime time,
    f32& cumulative_tracking_delta
) {
    // AGENT 2: AnimatedValue<f32> — evaluate per-frame.
    const f32 tracking_pixels = prop.pixels.evaluate(time);

    if (blend == TextPropertyBlendMode::Replace) {
        // Replace on the active side: overwrite position.x with the
        // cumulative sum BEFORE this glyph.  Same numerical result as
        // Add when the previous glyph left position.x at 0 (the
        // `make_initial_glyph_states` default), so the two blend modes
        // are observationally identical for a single-animator stack.
        // The multi-animator test in
        // tests/text/text_animator_property_tests.cpp documents the
        // diagnostic distinction.
        gs.position.x = cumulative_tracking_delta;
    } else {
        gs.position.x += cumulative_tracking_delta;
    }
    const f32 delta = tracking_pixels * weight;
    cumulative_tracking_delta += delta;
}

} // namespace chronon3d::detail
