#include <chronon3d/text/animation/text_animator_evaluator.hpp>

#include <chronon3d/text/font_engine.hpp>        // PlacedGlyphRun full type
#include <chronon3d/text/glyph_selector.hpp>      // evaluate_selectors, GlyphSelectorUnitMap

#include "text_property_stage.hpp"
#include "text_tracking_evaluator.hpp"

#include <limits>
#include <variant>

namespace chronon3d {

// Forward-declare the phase checker (avoids circular include).
namespace {
template <typename T>
constexpr bool is_pre_shaping() {
    return std::is_same_v<T, CharacterOffsetProperty>;
}
} // namespace

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
    const GlyphSelectorUnitMap& unit_map,
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

    // ── Cumulative word/line spacing ─────────────────────────────────────
    // Sample the animated amounts once: the time is constant for this frame.
    // The gap is inserted after a selected word/line, so it is driven by the
    // weight of the previous unit and then carried to every later glyph.
    f32 word_spacing_pixels = 0.0f;
    f32 line_spacing_pixels = 0.0f;
    bool has_word_spacing = false;
    bool has_line_spacing = false;
    for (const auto& prop : spec.properties) {
        if (const auto* spacing = std::get_if<WordSpacingProperty>(&prop)) {
            word_spacing_pixels += spacing->pixels.evaluate(time);
            has_word_spacing = true;
        } else if (const auto* spacing = std::get_if<LineSpacingProperty>(&prop)) {
            line_spacing_pixels += spacing->pixels.evaluate(time);
            has_line_spacing = true;
        }
    }

    constexpr u32 kNoUnit = std::numeric_limits<u32>::max();
    f32 cumulative_word_spacing = 0.0f;
    f32 cumulative_line_spacing = 0.0f;
    u32 previous_word = kNoUnit;
    u32 previous_line = kNoUnit;
    f32 previous_word_weight = 0.0f;
    f32 previous_line_weight = 0.0f;
    bool saw_word = false;
    bool saw_line = false;

    for (size_t gi = 0; gi < glyph_count; ++gi) {
        // Compute combined selector weight for this glyph (compiled path).
        SelectorWeight weight = evaluate_compiled_selectors(
            compiled_selectors,
            unit_map,
            static_cast<u32>(gi),
            time
        );

        f32 word_spacing_offset = 0.0f;
        f32 line_spacing_offset = 0.0f;
        if (has_word_spacing) {
            const u32 word = gi < unit_map.glyph_to_word.size()
                ? unit_map.glyph_to_word[gi] : kNoUnit;
            if (word != kNoUnit) {
                if (saw_word && word != previous_word)
                    cumulative_word_spacing +=
                        word_spacing_pixels * previous_word_weight;
                previous_word = word;
                previous_word_weight = weight;
                saw_word = true;
            }
            word_spacing_offset = cumulative_word_spacing;
        }
        if (has_line_spacing) {
            const u32 line = gi < unit_map.glyph_to_line.size()
                ? unit_map.glyph_to_line[gi] : 0u;
            if (saw_line && line != previous_line)
                cumulative_line_spacing +=
                    line_spacing_pixels * previous_line_weight;
            previous_line = line;
            previous_line_weight = weight;
            saw_line = true;
            line_spacing_offset = cumulative_line_spacing;
        }

        // Apply each property scaled by the weight.
        for (const auto& prop : spec.properties) {
            // FASE 2a: skip PreShaping properties — they're already
            // evaluated in `evaluate_pre_shaping_source()` before
            // the animator stack.  Skipping here avoids wasted
            // std::visit dispatch to the no-op branch.
            if (std::holds_alternative<CharacterOffsetProperty>(prop)) {
                continue;
            }

            // Use transform_mode or color_mode based on property type.
            TextPropertyBlendMode blend = spec.transform_mode;
            if (std::holds_alternative<FillColorProperty>(prop) ||
                std::holds_alternative<BackgroundColorProperty>(prop) ||
                std::holds_alternative<StrokeColorProperty>(prop)) {
                blend = spec.color_mode;
            }

            if (std::holds_alternative<WordSpacingProperty>(prop)) {
                if (blend == TextPropertyBlendMode::Replace)
                    glyph_states[gi].position.x = word_spacing_offset;
                else
                    glyph_states[gi].position.x += word_spacing_offset;
                continue;
            }

            if (std::holds_alternative<LineSpacingProperty>(prop)) {
                if (blend == TextPropertyBlendMode::Replace)
                    glyph_states[gi].position.y = line_spacing_offset;
                else
                    glyph_states[gi].position.y += line_spacing_offset;
                continue;
            }

            // TrackingProperty routes through the cumulative accumulator
            // BEFORE reaching the dispatcher. The order — apply, then
            // increment — mirrors the V1 reference at
            // `include/chronon3d/text/text_animator.hpp:333-339`.  // drift-class: historical — retired path (see git history)
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
