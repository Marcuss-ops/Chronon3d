#include <chronon3d/text/animation/text_animator_evaluator.hpp>

#include <chronon3d/text/font_engine.hpp>        // PlacedGlyphRun full type
#include <chronon3d/text/glyph_selector.hpp>      // evaluate_selectors, TextUnitMap

#include "text_property_stage.hpp"
#include "text_tracking_evaluator.hpp"

#include <variant>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// evaluate_animator — single-spec evaluator
// ═══════════════════════════════════════════════════════════════════════════
//
// Per-glyph pipeline (preserves original semantics verbatim):
//   1. Compute combined selector weight for glyph `gi`.
//   2. For each property in `spec.properties`:
//      a. Determine blend mode (transform_mode by default; color_mode for
//         FillColor/StrokeColor).
//      b. TrackingProperty → cumulative running-sum accumulator
//         (detail::apply_tracking_to_glyph).
//         The apply-first-then-increment ordering of the cumulative
//         delta is what produces AE-style "letter-spacing gap between
//         consecutive letters" semantics (see
//         text_tracking_evaluator.cpp for the carry-over note).
//      c. Else → canonical dispatcher
//         (detail::apply_property_to_glyph) for the per-glyph mutation.
//
// `placed` is passed through to the selector evaluator for word/line
// selectors that need cluster info to honour `exclude_spaces`. Pass
// nullptr (default) when the selector does not require cluster info.

void evaluate_animator(
    const TextAnimatorSpec& spec,
    const TextUnitMap& unit_map,
    std::vector<GlyphInstanceState>& glyph_states,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed
) {
    if (!spec.enabled) return;

    const size_t glyph_count = glyph_states.size();

    // ── Compile selectors once (Phase 1: still inside the frame call,
    //     but the CompiledSelector precomputes permutation + whitespace
    //     so the per-glyph loop is O(1) per glyph). ─────────────────────
    const auto compiled_selectors = compile_selectors(
        spec.selectors, unit_map, source, placed);

    // ── Cumulative tracking ──────────────────────────────────────────────
    f32 cumulative_tracking_delta = 0.0f;

    for (size_t gi = 0; gi < glyph_count; ++gi) {
        // Compute combined selector weight for this glyph (compiled path).
        SelectorWeight weight = evaluate_compiled_selectors(
            compiled_selectors,
            unit_map,
            static_cast<u32>(gi),
            time
        );

        // Apply each property scaled by the weight.
        for (const auto& prop : spec.properties) {
            // Use transform_mode or color_mode based on property type.
            TextPropertyBlendMode blend = spec.transform_mode;
            if (std::holds_alternative<FillColorProperty>(prop) ||
                std::holds_alternative<StrokeColorProperty>(prop)) {
                blend = spec.color_mode;
            }

            // TrackingProperty routes through the cumulative accumulator
            // BEFORE reaching the dispatcher. The order — apply, then
            // increment — mirrors the V1 reference at
            // `include/chronon3d/text/text_animator.hpp:333-339`.
            if (std::holds_alternative<TrackingProperty>(prop)) {
                detail::apply_tracking_to_glyph(
                    glyph_states[gi],
                    std::get<TrackingProperty>(prop),
                    weight,
                    blend,
                    time,
                    cumulative_tracking_delta
                );
                continue;
            }

            detail::apply_property_to_glyph(
                glyph_states[gi], prop, weight, blend, time
            );
        }
    }
}

} // namespace chronon3d
