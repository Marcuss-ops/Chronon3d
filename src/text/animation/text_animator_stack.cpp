#include <chronon3d/text/animation/text_animator_stack.hpp>

#include <chronon3d/text/animation/glyph_instance_state.hpp>    // reset_glyph_states_in_place
#include <chronon3d/text/animation/text_animator_evaluator.hpp>  // evaluate_animator
#include <chronon3d/text/font_engine.hpp>                        // PlacedGlyphRun full type
#include <chronon3d/text/text_run_layout.hpp>                     // ShapedFontSpan
#include <chronon3d/text/glyph_selector.hpp>                     // build_text_unit_map, TextUnitMap

#include <algorithm>

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
    SampleTime time,
    const std::vector<ShapedFontSpan>* span_styles
) {
    std::vector<GlyphInstanceState> states;
    auto unit_map = build_text_unit_map(placed, source);
    if (span_styles) {
        unit_map.glyph_semantic_id.assign(placed.glyphs.size(), {});
        for (const auto& span : *span_styles) {
            if (!span.semantic_id) continue;
            const auto end = std::min<std::size_t>(span.glyph_end,
                                                   unit_map.glyph_semantic_id.size());
            for (std::size_t glyph = span.glyph_begin; glyph < end; ++glyph)
                unit_map.glyph_semantic_id[glyph] = *span.semantic_id;
        }
    }
    evaluate_animator_stack_into(states, animators, placed, source, unit_map, time, span_styles);
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
    SampleTime time,
    const std::vector<ShapedFontSpan>* span_styles
) {
    reset_glyph_states_in_place(inout_states, placed);

    if (span_styles) {
        for (const auto& span : *span_styles) {
            const auto end = std::min<std::size_t>(span.glyph_end, inout_states.size());
            for (std::size_t glyph = span.glyph_begin; glyph < end; ++glyph) {
                auto& state = inout_states[glyph];
                if (span.fill_color_override) state.fill = *span.fill_color_override;
                if (span.stroke) {
                    const float font_size = span.font_size > 0.0f
                        ? span.font_size : placed.font_size;
                    state.stroke = span.stroke->color.value_or(state.fill);
                    state.stroke_width = span.stroke->width_em * font_size;
                }
                if (span.highlight && span.highlight->background) {
                    state.background = *span.highlight->background;
                    state.background_padding_em = span.highlight->padding_em.value_or(0.5f);
                    state.background_radius_em = span.highlight->radius_em.value_or(0.0f);
                }
            }
        }
    }

    for (const auto& animator : animators) {
        evaluate_animator(animator, unit_map, inout_states, source, time, &placed);
    }
}

} // namespace chronon3d
