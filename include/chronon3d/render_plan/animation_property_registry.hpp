#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// animation_property_registry.hpp — canonical layer animation property table
// ═══════════════════════════════════════════════════════════════════════════
//
// One flat compile-time table of every `layers[].animation.tracks[].property`
// name the RenderPlan decode/evaluate pipeline understands. The decoder gate
// in `render_plan_decoder_layer.cpp` fails closed on any track whose property
// is absent from this table; an unregistered property can no longer decode
// silently and then be ignored by the compiler's property dispatch.
//
// Rules (match the existing X-macro discipline of effect_catalog.def):
//   - every name is declared EXACTLY once, here;
//   - the shadow family is enumerated explicitly (shadow_<0..2>_<suffix>)
//     so the table stays a flat, greppable inventory instead of a second
//     naming authority;
//   - `arity` mirrors the value-width contract already enforced downstream:
//     1 = scalar, 3 = vec3, 4 = RGBA, 0 = variable arity (validated per
//     family by the existing decoder passes — the registry gates names, the
//     per-property passes gate values).
//
// Adding a new animatable property: append one CHRONON_ANIMATION_PROPERTY
// row, wire the compiler dispatch, and extend the decode gate tests. A row
// without a compiler consumer (or a consumer without a row) must fail the
// registry census test.
// ═══════════════════════════════════════════════════════════════════════════

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace chronon3d::render_plan {

enum class AnimationPropertyId : std::uint8_t {
    Opacity,
    Blur,
    Tracking,
    BaselineShift,
    CharacterOffset,
    Anchor,
    BackgroundColor,
    BackgroundPaddingEm,
    BackgroundRadiusEm,
    FillColor,
    Position,
    PositionX,
    PositionY,
    PositionZ,
    Rotation,
    RotationX,
    RotationY,
    RotationZ,
    Scale,
    ScaleX,
    ScaleY,
    ScaleZ,
    Skew,
    SkewAxis,
    StrokeColor,
    StrokeDashOffset,
    StrokeWidth,
    Shadow0Opacity,
    Shadow0Blur,
    Shadow0OffsetX,
    Shadow0OffsetY,
    Shadow0Color,
    Shadow1Opacity,
    Shadow1Blur,
    Shadow1OffsetX,
    Shadow1OffsetY,
    Shadow1Color,
    Shadow2Opacity,
    Shadow2Blur,
    Shadow2OffsetX,
    Shadow2OffsetY,
    Shadow2Color,
    Count,
};

struct AnimationPropertyRow {
    AnimationPropertyId id;
    std::string_view name;
    // 1 = scalar; 3 = three components; 4 = RGBA; 0 = variable arity
    // (component count validated by the family-specific decoder pass).
    std::uint8_t arity;
};

#define CHRONON_ANIMATION_PROPERTY(id_enum, name_literal, arity_value) \
    { AnimationPropertyId::id_enum, name_literal, arity_value },

inline constexpr std::array<AnimationPropertyRow,
    static_cast<std::size_t>(AnimationPropertyId::Count)>
    kAnimationPropertyTable{{
        CHRONON_ANIMATION_PROPERTY(Opacity, "opacity", 1)
        CHRONON_ANIMATION_PROPERTY(Blur, "blur", 1)
        CHRONON_ANIMATION_PROPERTY(Tracking, "tracking", 1)
        CHRONON_ANIMATION_PROPERTY(BaselineShift, "baseline_shift", 1)
        CHRONON_ANIMATION_PROPERTY(CharacterOffset, "character_offset", 1)
        CHRONON_ANIMATION_PROPERTY(Anchor, "anchor", 0)
        CHRONON_ANIMATION_PROPERTY(BackgroundColor, "background_color", 4)
        CHRONON_ANIMATION_PROPERTY(BackgroundPaddingEm, "background_padding_em", 1)
        CHRONON_ANIMATION_PROPERTY(BackgroundRadiusEm, "background_radius_em", 1)
        CHRONON_ANIMATION_PROPERTY(FillColor, "fill_color", 4)
        CHRONON_ANIMATION_PROPERTY(Position, "position", 0)
        CHRONON_ANIMATION_PROPERTY(PositionX, "position_x", 1)
        CHRONON_ANIMATION_PROPERTY(PositionY, "position_y", 1)
        CHRONON_ANIMATION_PROPERTY(PositionZ, "position_z", 1)
        CHRONON_ANIMATION_PROPERTY(Rotation, "rotation", 0)
        CHRONON_ANIMATION_PROPERTY(RotationX, "rotation_x", 1)
        CHRONON_ANIMATION_PROPERTY(RotationY, "rotation_y", 1)
        CHRONON_ANIMATION_PROPERTY(RotationZ, "rotation_z", 1)
        CHRONON_ANIMATION_PROPERTY(Scale, "scale", 0)
        CHRONON_ANIMATION_PROPERTY(ScaleX, "scale_x", 1)
        CHRONON_ANIMATION_PROPERTY(ScaleY, "scale_y", 1)
        CHRONON_ANIMATION_PROPERTY(ScaleZ, "scale_z", 1)
        CHRONON_ANIMATION_PROPERTY(Skew, "skew", 1)
        CHRONON_ANIMATION_PROPERTY(SkewAxis, "skew_axis", 1)
        CHRONON_ANIMATION_PROPERTY(StrokeColor, "stroke_color", 4)
        CHRONON_ANIMATION_PROPERTY(StrokeDashOffset, "stroke_dash_offset", 1)
        CHRONON_ANIMATION_PROPERTY(StrokeWidth, "stroke_width", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow0Opacity, "shadow_0_opacity", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow0Blur, "shadow_0_blur", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow0OffsetX, "shadow_0_offset_x", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow0OffsetY, "shadow_0_offset_y", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow0Color, "shadow_0_color", 4)
        CHRONON_ANIMATION_PROPERTY(Shadow1Opacity, "shadow_1_opacity", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow1Blur, "shadow_1_blur", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow1OffsetX, "shadow_1_offset_x", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow1OffsetY, "shadow_1_offset_y", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow1Color, "shadow_1_color", 4)
        CHRONON_ANIMATION_PROPERTY(Shadow2Opacity, "shadow_2_opacity", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow2Blur, "shadow_2_blur", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow2OffsetX, "shadow_2_offset_x", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow2OffsetY, "shadow_2_offset_y", 1)
        CHRONON_ANIMATION_PROPERTY(Shadow2Color, "shadow_2_color", 4)
    }};

#undef CHRONON_ANIMATION_PROPERTY

/// Exact lookup. Returns nullptr when the name is not a registered layer
/// animation property — the decoder must reject the track.
[[nodiscard]] inline const AnimationPropertyRow* find_animation_property(
    std::string_view name) noexcept {
    for (const auto& row : kAnimationPropertyTable)
        if (row.name == name) return &row;
    return nullptr;
}

/// Bounded Levenshtein distance for nearest-name diagnostics. Returns a value
/// greater than `max_distance` as soon as the bound is exceeded (early exit;
/// the strings here are short so this stays cheap).
[[nodiscard]] inline std::size_t animation_property_edit_distance(
    std::string_view left, std::string_view right,
    std::size_t max_distance) noexcept {
    if (left.size() > right.size() + max_distance ||
        right.size() > left.size() + max_distance)
        return max_distance + 1;
    std::size_t previous[64];
    std::size_t current[64];
    const std::size_t columns = right.size() + 1;
    for (std::size_t c = 0; c < columns; ++c) previous[c] = c;
    for (std::size_t r = 1; r <= left.size(); ++r) {
        current[0] = r;
        std::size_t row_min = current[0];
        for (std::size_t c = 1; c < columns; ++c) {
            const std::size_t substitute = previous[c - 1] +
                (left[r - 1] == right[c - 1] ? 0u : 1u);
            const std::size_t insert = previous[c] + 1;
            const std::size_t erase = current[c - 1] + 1;
            current[c] = substitute < insert   ? substitute
                         : insert < erase      ? insert
                                               : erase;
            if (current[c] < row_min) row_min = current[c];
        }
        if (row_min > max_distance) return max_distance + 1;
        for (std::size_t c = 0; c < columns; ++c) previous[c] = current[c];
    }
    return previous[right.size()];
}

/// Nearest registered name within an edit-distance budget, for decode error
/// messages. Empty view when nothing is close enough.
[[nodiscard]] inline std::string_view nearest_animation_property_name(
    std::string_view name) noexcept {
    constexpr std::size_t kMaxSuggestionDistance = 2;
    std::string_view best;
    std::size_t best_distance = kMaxSuggestionDistance + 1;
    for (const auto& row : kAnimationPropertyTable) {
        const std::size_t distance = animation_property_edit_distance(
            name, row.name, kMaxSuggestionDistance);
        if (distance < best_distance) {
            best_distance = distance;
            best = row.name;
        }
    }
    return best_distance <= kMaxSuggestionDistance ? best : std::string_view{};
}

}  // namespace chronon3d::render_plan
