#include "text_property_stage.hpp"

#include <type_traits>
#include <variant>

namespace chronon3d::detail {

// ═══════════════════════════════════════════════════════════════════════════
// apply_property_to_glyph — single canonical property dispatcher
// ═══════════════════════════════════════════════════════════════════════════
//
// ONE source of truth for "what does property X do to a glyph state".
// All 13 PostLayout TextAnimatorProperty variants are dispatched here.
// CharacterOffsetProperty was moved to the PreShaping phase (FASE 2a)
// — it is evaluated in `apply_character_offset_to_source()` in
// src/text/animation/text_pre_shaping.cpp BEFORE shaping.
//
// TrackingProperty is intentionally a no-op here. The cumulative
// across-glyph bookkeeping for tracking lives in
// `detail::apply_tracking_to_glyph` (text_tracking_evaluator.cpp).

void apply_property_to_glyph(
    GlyphInstanceState& gs,
    const TextAnimatorProperty& prop,
    SelectorWeight weight,
    TextPropertyBlendMode blend,
    SampleTime time
) {
    // Short-circuit for zero weight (preserves prior behaviour).
    if (weight <= 0.0f) return;

    std::visit([&](const auto& p) {
        using T = std::decay_t<decltype(p)>;

        if constexpr (std::is_same_v<T, PositionProperty>) {
            // AGENT 2: AnimatedValue<Vec3> — evaluate per-frame.
            const Vec3 v = p.value.evaluate(time);
            const Vec3 delta = v * weight;
            if (blend == TextPropertyBlendMode::Replace) {
                gs.position = delta;
            } else {
                gs.position = gs.position + delta;
            }
        }
        else if constexpr (std::is_same_v<T, ScaleProperty>) {
            // AGENT 2: AnimatedValue<Vec3> — evaluate per-frame.
            const Vec3 v = p.value.evaluate(time);
            // Scale: compute the delta from identity (1,1,1).
            const Vec3 delta = {
                (v.x - 1.0f) * weight,
                (v.y - 1.0f) * weight,
                (v.z - 1.0f) * weight
            };
            if (blend == TextPropertyBlendMode::Replace) {
                gs.scale = {1.0f + delta.x, 1.0f + delta.y, 1.0f + delta.z};
            } else if (blend == TextPropertyBlendMode::Multiply) {
                // Multiply: lerp from identity to target, then multiply.
                const Vec3 factor = {
                    1.0f + (v.x - 1.0f) * weight,
                    1.0f + (v.y - 1.0f) * weight,
                    1.0f + (v.z - 1.0f) * weight
                };
                gs.scale = {gs.scale.x * factor.x, gs.scale.y * factor.y, gs.scale.z * factor.z};
            } else {
                // Add: accumulate deltas (scale from 50% to 150% → add -0.5 or +0.5).
                gs.scale = {gs.scale.x + delta.x, gs.scale.y + delta.y, gs.scale.z + delta.z};
            }
        }
        else if constexpr (std::is_same_v<T, RotationProperty>) {
            const Vec3 delta = p.degrees * weight;
            if (blend == TextPropertyBlendMode::Replace) {
                gs.rotation = delta;
            } else {
                gs.rotation = gs.rotation + delta;
            }
        }
        else if constexpr (std::is_same_v<T, SkewProperty>) {
            if (blend == TextPropertyBlendMode::Replace) {
                gs.skew = p.angle * weight;
                gs.skew_axis = p.axis;
            } else {
                gs.skew += p.angle * weight;
                gs.skew_axis = p.axis;  // last-writer-wins for axis.
            }
        }
        else if constexpr (std::is_same_v<T, AnchorProperty>) {
            const Vec3 delta = p.value * weight;
            if (blend == TextPropertyBlendMode::Replace) {
                gs.anchor = delta;
            } else {
                gs.anchor = gs.anchor + delta;
            }
        }
        else if constexpr (std::is_same_v<T, OpacityProperty>) {
            // AGENT 2: AnimatedValue<f32> — evaluate per-frame.
            const f32 v = p.value.evaluate(time);
            // Opacity: lerp from 1.0 to target, multiply.
            const f32 animated = 1.0f + (v - 1.0f) * weight;
            if (blend == TextPropertyBlendMode::Replace) {
                gs.opacity = animated;
            } else if (blend == TextPropertyBlendMode::Multiply) {
                gs.opacity *= animated;
            } else {
                gs.opacity *= animated;  // additive for opacity = multiply semantics.
            }
        }
        else if constexpr (std::is_same_v<T, BlurProperty>) {
            // AGENT 2: AnimatedValue<f32> — evaluate per-frame.
            const f32 v = p.radius.evaluate(time);
            if (blend == TextPropertyBlendMode::Replace) {
                gs.blur = v * weight;
            } else {
                gs.blur += v * weight;
            }
        }
        else if constexpr (std::is_same_v<T, FillColorProperty>) {
            // Color: lerp from current to target.
            const Color lerped = {
                gs.fill.r + (p.color.r - gs.fill.r) * weight,
                gs.fill.g + (p.color.g - gs.fill.g) * weight,
                gs.fill.b + (p.color.b - gs.fill.b) * weight,
                gs.fill.a + (p.color.a - gs.fill.a) * weight,
            };
            if (blend == TextPropertyBlendMode::Replace) {
                gs.fill = lerped;
            } else {
                gs.fill = lerped;  // additive = lerp for colors.
            }
        }
        else if constexpr (std::is_same_v<T, BackgroundColorProperty>) {
            const Color lerped = {
                gs.background.r + (p.color.r - gs.background.r) * weight,
                gs.background.g + (p.color.g - gs.background.g) * weight,
                gs.background.b + (p.color.b - gs.background.b) * weight,
                gs.background.a + (p.color.a - gs.background.a) * weight,
            };
            if (blend == TextPropertyBlendMode::Replace) {
                gs.background = lerped;
            } else {
                gs.background = lerped;
            }
        }
        else if constexpr (std::is_same_v<T, StrokeColorProperty>) {
            const Color lerped = {
                gs.stroke.r + (p.color.r - gs.stroke.r) * weight,
                gs.stroke.g + (p.color.g - gs.stroke.g) * weight,
                gs.stroke.b + (p.color.b - gs.stroke.b) * weight,
                gs.stroke.a + (p.color.a - gs.stroke.a) * weight,
            };
            if (blend == TextPropertyBlendMode::Replace) {
                gs.stroke = lerped;
            } else {
                gs.stroke = lerped;
            }
        }
        else if constexpr (std::is_same_v<T, StrokeWidthProperty>) {
            if (blend == TextPropertyBlendMode::Replace) {
                gs.stroke_width = p.width * weight;
            } else {
                gs.stroke_width += p.width * weight;
            }
        }
        else if constexpr (std::is_same_v<T, TrackingProperty>) {
            // Tracking is handled in evaluate_animator via a cumulative
            // accumulator that spans this animator's per-glyph iterations
            // (port from V1 include/chronon3d/text/text_animator.hpp:325-337).  // drift-allow: stale-ref
            // We no-op here to avoid double-counting; the evaluator routes
            // TrackingProperty through detail::apply_tracking_to_glyph
            // BEFORE this dispatcher is reached.
            (void)p;
            (void)gs;
        }
        else if constexpr (std::is_same_v<T, BaselineShiftProperty>) {
            if (blend == TextPropertyBlendMode::Replace) {
                gs.baseline_shift = p.pixels * weight;
            } else {
                gs.baseline_shift += p.pixels * weight;
            }
        }
        else if constexpr (std::is_same_v<T, CharacterOffsetProperty>) {
            // FASE 2a: CharacterOffsetProperty is now a PreShaping
            // property evaluated in `apply_character_offset_to_source()`
            // BEFORE HarfBuzz shaping.  It no longer writes to
            // GlyphInstanceState.character_offset (which is now always 0).
            // This branch exists only so the variant visitor compiles
            // — it should never be reached because the PreShaping phase
            // strips CharacterOffsetProperty before this dispatcher runs.
            (void)p;
            (void)gs;
        }
    }, prop);
}

} // namespace chronon3d::detail
