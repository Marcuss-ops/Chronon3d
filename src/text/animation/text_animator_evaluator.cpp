#include <chronon3d/text/animation/text_animator_evaluator.hpp>

#include <chronon3d/text/font_engine.hpp>        // PlacedGlyphRun full type
#include <chronon3d/text/glyph_selector.hpp>      // evaluate_selectors, GlyphSelectorUnitMap

#include "text_property_stage.hpp"
#include "text_tracking_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

namespace chronon3d {

// Forward-declare the phase checker (avoids circular include).
namespace {
template <typename T>
constexpr bool is_pre_shaping() {
    return std::is_same_v<T, CharacterOffsetProperty> ||
           std::is_same_v<T, CharacterValueProperty>;
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

    // ── P1-04 — run-level line anchor ────────────────────────────────────
    // The mode is a static enum; the shift is derived once from the authored
    // layout extents and applied to every glyph.  Line Spacing starts its
    // cumulative gap at zero on line 0, so the two compose without shifting
    // the first line twice.
    bool has_line_anchor = false;
    LineAnchorMode line_anchor_mode{LineAnchorMode::Top};
    for (const auto& prop : spec.properties) {
        if (const auto* anchor = std::get_if<LineAnchorProperty>(&prop)) {
            line_anchor_mode = anchor->mode;
            has_line_anchor = true;
        }
    }
    f32 line_anchor_shift = 0.0f;
    if (has_line_anchor && placed != nullptr && glyph_count > 0) {
        const f32 ascent = placed->ascent;
        const f32 block_height = placed->total_height;
        switch (line_anchor_mode) {
            case LineAnchorMode::Top:
                line_anchor_shift = ascent;
                break;
            case LineAnchorMode::Center:
                line_anchor_shift = ascent - block_height * 0.5f;
                break;
            case LineAnchorMode::Bottom:
                line_anchor_shift = ascent - block_height;
                break;
        }
    }

    // ── P1-05 — anchor point grouping ────────────────────────────────────
    // Resolve the pivot once per animator, before any transform property runs,
    // so scale/rotation in this animator rotate around the authored group box.
    const AnchorPointGroupingProperty* grouping = nullptr;
    for (const auto& prop : spec.properties) {
        if (const auto* candidate = std::get_if<AnchorPointGroupingProperty>(&prop)) {
            grouping = candidate;
        }
    }
    if (grouping != nullptr && placed != nullptr && glyph_count > 0) {
        const size_t placed_count = std::min(glyph_count, placed->glyphs.size());
        std::vector<u32> group_of(glyph_count, 0u);
        u32 max_group = 0;
        for (size_t gi = 0; gi < placed_count; ++gi) {
            u32 group = 0;
            switch (grouping->mode) {
                case AnchorPointGrouping::Character:
                    group = static_cast<u32>(gi);
                    break;
                case AnchorPointGrouping::Word:
                    group = gi < unit_map.glyph_to_word.size()
                        ? unit_map.glyph_to_word[gi] : static_cast<u32>(gi);
                    break;
                case AnchorPointGrouping::Line:
                    group = gi < unit_map.glyph_to_line.size()
                        ? unit_map.glyph_to_line[gi] : 0u;
                    break;
                case AnchorPointGrouping::All:
                    group = 0u;
                    break;
            }
            if (group == kNoUnit) group = static_cast<u32>(gi);
            group_of[gi] = group;
            max_group = std::max(max_group, group);
        }

        constexpr f32 kInf = std::numeric_limits<f32>::infinity();
        std::vector<f32> group_left(static_cast<size_t>(max_group) + 1u, kInf);
        std::vector<f32> group_right(static_cast<size_t>(max_group) + 1u, -kInf);
        std::vector<f32> group_top(static_cast<size_t>(max_group) + 1u, kInf);
        std::vector<f32> group_bottom(static_cast<size_t>(max_group) + 1u, -kInf);
        for (size_t gi = 0; gi < placed_count; ++gi) {
            const auto& placed_glyph = placed->glyphs[gi];
            const u32 group = group_of[gi];
            group_left[group] = std::min(group_left[group], placed_glyph.x);
            group_right[group] = std::max(
                group_right[group], placed_glyph.x + placed_glyph.advance_x);
            group_top[group] = std::min(
                group_top[group], placed_glyph.y - placed->ascent);
            group_bottom[group] = std::max(
                group_bottom[group], placed_glyph.y + placed->descent);
        }

        for (size_t gi = 0; gi < placed_count; ++gi) {
            const u32 group = group_of[gi];
            if (!std::isfinite(group_left[group]) ||
                !std::isfinite(group_right[group]) ||
                !std::isfinite(group_top[group]) ||
                !std::isfinite(group_bottom[group])) {
                continue;
            }
            const f32 pivot_x =
                (group_left[group] + group_right[group]) * 0.5f;
            const f32 pivot_y =
                (group_top[group] + group_bottom[group]) * 0.5f;
            glyph_states[gi].anchor.x =
                pivot_x - glyph_states[gi].layout_position.x;
            glyph_states[gi].anchor.y =
                pivot_y - glyph_states[gi].layout_position.y;
            glyph_states[gi].anchor.z = 0.0f;
        }
    }

    // ── P1-03 — character alignment line extents ─────────────────────────
    // One line box per layout line, resolved from the authored placed run.
    const CharacterAlignmentProperty* alignment = nullptr;
    for (const auto& prop : spec.properties) {
        if (const auto* candidate = std::get_if<CharacterAlignmentProperty>(&prop)) {
            alignment = candidate;
        }
    }
    std::vector<f32> line_left;
    std::vector<f32> line_right;
    if (alignment != nullptr && placed != nullptr && glyph_count > 0) {
        const size_t placed_count = std::min(glyph_count, placed->glyphs.size());
        const u32 max_line = unit_map.glyph_to_line.empty()
            ? 0u
            : *std::max_element(unit_map.glyph_to_line.begin(),
                                unit_map.glyph_to_line.end());
        constexpr f32 kInf = std::numeric_limits<f32>::infinity();
        line_left.assign(static_cast<size_t>(max_line) + 1u, kInf);
        line_right.assign(static_cast<size_t>(max_line) + 1u, -kInf);
        for (size_t gi = 0; gi < placed_count; ++gi) {
            const u32 line = gi < unit_map.glyph_to_line.size()
                ? unit_map.glyph_to_line[gi] : 0u;
            if (line >= line_left.size()) continue;
            const auto& placed_glyph = placed->glyphs[gi];
            line_left[line] = std::min(line_left[line], placed_glyph.x);
            line_right[line] = std::max(
                line_right[line], placed_glyph.x + placed_glyph.advance_x);
        }
    }

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

        if (has_line_anchor) {
            if (spec.transform_mode == TextPropertyBlendMode::Replace)
                glyph_states[gi].position.y = line_anchor_shift;
            else
                glyph_states[gi].position.y += line_anchor_shift;
        }

        // ── P1-03 — align the selected glyph cell inside its line box ────
        if (alignment != nullptr && placed != nullptr &&
            gi < placed->glyphs.size()) {
            const u32 line = gi < unit_map.glyph_to_line.size()
                ? unit_map.glyph_to_line[gi] : 0u;
            if (line < line_left.size() && std::isfinite(line_left[line]) &&
                std::isfinite(line_right[line])) {
                const auto& placed_glyph = placed->glyphs[gi];
                f32 target_x = line_left[line];
                if (alignment->mode == CharacterAlignmentMode::Center) {
                    target_x = (line_left[line] + line_right[line] -
                                placed_glyph.advance_x) * 0.5f;
                } else if (alignment->mode == CharacterAlignmentMode::Right) {
                    target_x = line_right[line] - placed_glyph.advance_x;
                }
                const f32 shift = (target_x - placed_glyph.x) * weight;
                if (spec.transform_mode == TextPropertyBlendMode::Replace)
                    glyph_states[gi].position.x = shift;
                else
                    glyph_states[gi].position.x += shift;
            }
        }

        // Apply each property scaled by the weight.
        for (const auto& prop : spec.properties) {
            // FASE 2a: skip PreShaping properties — they're already
            // evaluated in `evaluate_pre_shaping_source()` before
            // the animator stack.  Skipping here avoids wasted
            // std::visit dispatch to the no-op branch.
            if (std::holds_alternative<CharacterOffsetProperty>(prop) ||
                std::holds_alternative<CharacterValueProperty>(prop) ||
                std::holds_alternative<CharacterAlignmentProperty>(prop) ||
                std::holds_alternative<AnchorPointGroupingProperty>(prop) ||
                std::holds_alternative<LineAnchorProperty>(prop) ||
                std::holds_alternative<ReflowWordSpacingProperty>(prop) ||
                std::holds_alternative<ReflowLineSpacingProperty>(prop)) {
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
