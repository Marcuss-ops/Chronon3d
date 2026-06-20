#include <chronon3d/text/text_animator_property.hpp>

#include <algorithm>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// Initial glyph state factory
// ═══════════════════════════════════════════════════════════════════════════

std::vector<GlyphInstanceState> make_initial_glyph_states(
    const PlacedGlyphRun& placed
) {
    std::vector<GlyphInstanceState> states;
    states.reserve(placed.glyphs.size());

    for (const auto& g : placed.glyphs) {
        GlyphInstanceState gs;
        gs.glyph_id = g.glyph_id;
        gs.layout_position = {g.x, g.y};
        gs.position = {0.0f, 0.0f, 0.0f};
        gs.scale = {1.0f, 1.0f, 1.0f};
        gs.rotation = {0.0f, 0.0f, 0.0f};
        gs.anchor = {0.0f, 0.0f, 0.0f};
        gs.skew = 0.0f;
        gs.skew_axis = 0.0f;
        gs.opacity = 1.0f;
        gs.blur = 0.0f;
        gs.baseline_shift = 0.0f;
        gs.character_offset = 0;
        gs.fill = {1.0f, 1.0f, 1.0f, 1.0f};
        gs.stroke = {0.0f, 0.0f, 0.0f, 0.0f};
        gs.stroke_width = 0.0f;
        states.push_back(gs);
    }

    return states;
}

// ═══════════════════════════════════════════════════════════════════════════
// Single animator evaluation (mutates glyph states in-place)
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// Apply a single property to a glyph state, scaled by weight.
void apply_property_to_glyph(
    GlyphInstanceState& gs,
    const TextAnimatorProperty& prop,
    SelectorWeight weight,
    TextPropertyBlendMode blend
) {
    // Short-circuit for zero weight
    if (weight <= 0.0f) return;

    std::visit([&](const auto& p) {
        using T = std::decay_t<decltype(p)>;

        if constexpr (std::is_same_v<T, PositionProperty>) {
            const Vec3 delta = p.value * weight;
            if (blend == TextPropertyBlendMode::Replace) {
                gs.position = delta;
            } else {
                gs.position = gs.position + delta;
            }
        }
        else if constexpr (std::is_same_v<T, ScaleProperty>) {
            // Scale: compute the delta from identity (1,1,1)
            const Vec3 delta = {
                (p.value.x - 1.0f) * weight,
                (p.value.y - 1.0f) * weight,
                (p.value.z - 1.0f) * weight
            };
            if (blend == TextPropertyBlendMode::Replace) {
                gs.scale = {1.0f + delta.x, 1.0f + delta.y, 1.0f + delta.z};
            } else if (blend == TextPropertyBlendMode::Multiply) {
                // Multiply: lerp from identity to target, then multiply
                const Vec3 factor = {
                    1.0f + (p.value.x - 1.0f) * weight,
                    1.0f + (p.value.y - 1.0f) * weight,
                    1.0f + (p.value.z - 1.0f) * weight
                };
                gs.scale = {gs.scale.x * factor.x, gs.scale.y * factor.y, gs.scale.z * factor.z};
            } else {
                // Add: accumulate deltas (scale from 50% to 150% → add -0.5 or +0.5)
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
                gs.skew_axis = p.axis;  // last-writer-wins for axis
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
            // Opacity: lerp from 1.0 to target, multiply
            const f32 animated = 1.0f + (p.value - 1.0f) * weight;
            if (blend == TextPropertyBlendMode::Replace) {
                gs.opacity = animated;
            } else if (blend == TextPropertyBlendMode::Multiply) {
                gs.opacity *= animated;
            } else {
                gs.opacity *= animated;  // additive for opacity = multiply semantics
            }
        }
        else if constexpr (std::is_same_v<T, BlurProperty>) {
            if (blend == TextPropertyBlendMode::Replace) {
                gs.blur = p.radius * weight;
            } else {
                gs.blur += p.radius * weight;
            }
        }
        else if constexpr (std::is_same_v<T, FillColorProperty>) {
            // Color: lerp from current to target
            const Color lerped = {
                gs.fill.r + (p.color.r - gs.fill.r) * weight,
                gs.fill.g + (p.color.g - gs.fill.g) * weight,
                gs.fill.b + (p.color.b - gs.fill.b) * weight,
                gs.fill.a + (p.color.a - gs.fill.a) * weight,
            };
            if (blend == TextPropertyBlendMode::Replace) {
                gs.fill = lerped;
            } else {
                gs.fill = lerped;  // additive = lerp for colors
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
            // (port from V1 include/chronon3d/text/text_animator.hpp:325-337).
            // We no-op here to avoid double-counting; this branch is reached
            // only if a caller invokes apply_property_to_glyph directly.
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
            // Character offset (code-point shift) uses a dedicated field
            // to avoid collision with PositionProperty's position.x.
            if (blend == TextPropertyBlendMode::Replace) {
                gs.character_offset = static_cast<i32>(static_cast<f32>(p.offset) * weight);
            } else {
                gs.character_offset += static_cast<i32>(static_cast<f32>(p.offset) * weight);
            }
        }
    }, prop);
}

} // anonymous namespace

void evaluate_animator(
    const TextAnimatorSpec& spec,
    const TextUnitMap& unit_map,
    std::vector<GlyphInstanceState>& glyph_states,
    std::string_view source,
    SampleTime time,
    const PlacedGlyphRun* placed = nullptr
) {
    if (!spec.enabled) return;

    const size_t glyph_count = glyph_states.size();

    // ── Cumulative tracking ──────────────────────────────────────────────
    // After Effects-style tracking is the running letter-spacing gap between
    // consecutive glyphs.  Without accumulating across iterations, every
    // glyph receives the same delta and the text moves but does not widen.
    //
    // Carry-over from include/chronon3d/text/text_animator.hpp:325-337 where
    // V1 wrote `cumulative_tracking += m_tracking` then `glyph.x += cumul…`.
    // V2 reproduces the same shape, but the per-glyph delta is gated by the
    // selector weight, so weighted-out glyphs don't widen the spacing.
    f32 cumulative_tracking_delta = 0.0f;

    for (size_t gi = 0; gi < glyph_count; ++gi) {
        // Compute combined selector weight for this glyph
        SelectorWeight weight = evaluate_selectors(
            spec.selectors,
            unit_map,
            static_cast<u32>(gi),
            source,
            time,
            placed
        );

        // Apply each property scaled by the weight
        for (const auto& prop : spec.properties) {
            // Use transform_mode or color_mode based on property type
            TextPropertyBlendMode blend = spec.transform_mode;
            if (std::holds_alternative<FillColorProperty>(prop) ||
                std::holds_alternative<StrokeColorProperty>(prop)) {
                blend = spec.color_mode;
            }

            // TrackingProperty routes through the cumulative accumulator
            // BEFORE reaching apply_property_to_glyph.  The order — apply,
            // then increment — mirrors the V1 reference at
            // include/chronon3d/text/text_animator.hpp:333-339:
            //
            //     f32 adjusted_x = gu.x + cumulative_tracking;
            //     ... use adjusted_x ...
            //     cumulative_tracking += m_tracking;
            //
            // So the FIRST glyph receives cumulative_tracking = 0 (no letter
            // preceding it to spread from), and subsequent glyphs each
            // receive the running sum from previous iterations.  This is the
            // AE-style "letter-spacing gap between consecutive letters"
            // semantic.
            if (std::holds_alternative<TrackingProperty>(prop)) {
                const auto& tp = std::get<TrackingProperty>(prop);
                auto& gs = glyph_states[gi];
                if (blend == TextPropertyBlendMode::Replace) {
                    gs.position.x = cumulative_tracking_delta;
                } else {
                    gs.position.x += cumulative_tracking_delta;
                }
                const f32 delta = tp.pixels * weight;
                cumulative_tracking_delta += delta;
                continue;
            }

            apply_property_to_glyph(glyph_states[gi], prop, weight, blend);
        }
    }
}

std::vector<GlyphInstanceState> evaluate_animator_stack(
    const std::vector<TextAnimatorSpec>& animators,
    const PlacedGlyphRun& placed,
    std::string_view source,
    SampleTime time
) {
    auto states = make_initial_glyph_states(placed);
    auto unit_map = build_text_unit_map(placed, source);

    for (const auto& animator : animators) {
        evaluate_animator(animator, unit_map, states, source, time, &placed);
    }

    return states;
}

} // namespace chronon3d
