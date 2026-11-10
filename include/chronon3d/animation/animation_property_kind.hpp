#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace chronon3d {

// Canonical animation property taxonomy shared by render-plan compilation and
// the typed-program lowering. Dotted component names are absolute values;
// underscore position components are additive offsets.
enum class AnimationPropertyKind : std::uint8_t {
    Unknown,
    Opacity,
    PositionX,
    PositionY,
    PositionZ,
    PositionXOffset,
    PositionYOffset,
    PositionZOffset,
    ScaleX,
    ScaleY,
    ScaleZ,
    UniformScale,
    RotationX,
    RotationY,
    RotationZ,
    TimeRemap,
    PositionVector,
    RotationVector,
    Blur,
    Tracking,
    Skew,
    SkewAxis,
    FillColor,
    Anchor,
    BaselineShift,
    BackgroundColor,
    BackgroundPadding,
    BackgroundRadius,
    StrokeColor,
    StrokeWidth,
    CharacterOffset,
    LightIntensity,
    LightColor,
    LightDirection,
    // New values are appended so existing enum numeric values stay stable.
    WordSpacing,
    LineSpacing,
    GlyphShadowColor,
    GlyphShadowOffset,
    GlyphShadowBlur,
    GlyphShadowOpacity,
    GlyphGlowColor,
    GlyphGlowRadius,
    GlyphGlowIntensity,
    GlyphGradientStartColor,
    GlyphGradientEndColor,
    GlyphGradientAngle,
    ReflowWordSpacing,
    ReflowLineSpacing,
    LineAnchor,
    CharacterValue,
    CharacterAlignment,
    AnchorGrouping,
};

[[nodiscard]] inline AnimationPropertyKind animation_property_kind(std::string_view name) {
    using Kind = AnimationPropertyKind;
    static const std::unordered_map<std::string_view, Kind> kinds{
        {"opacity", Kind::Opacity},
        {"position.x", Kind::PositionX}, {"position.y", Kind::PositionY},
        {"position.z", Kind::PositionZ},
        {"position_x", Kind::PositionXOffset}, {"position_y", Kind::PositionYOffset},
        {"position_z", Kind::PositionZOffset}, {"position", Kind::PositionVector},
        // Dotted and underscored scale/rotation component spellings are aliases.
        {"scale.x", Kind::ScaleX}, {"scale.y", Kind::ScaleY}, {"scale.z", Kind::ScaleZ},
        {"scale_x", Kind::ScaleX}, {"scale_y", Kind::ScaleY}, {"scale_z", Kind::ScaleZ},
        {"scale", Kind::UniformScale},
        {"rotation.x", Kind::RotationX}, {"rotation.y", Kind::RotationY},
        {"rotation.z", Kind::RotationZ}, {"rotation_x", Kind::RotationX},
        {"rotation_y", Kind::RotationY}, {"rotation_z", Kind::RotationZ},
        {"rotation", Kind::RotationVector}, {"time_remap", Kind::TimeRemap},
        {"blur", Kind::Blur}, {"tracking", Kind::Tracking}, {"skew", Kind::Skew},
        {"skew_axis", Kind::SkewAxis}, {"fill_color", Kind::FillColor},
        {"anchor", Kind::Anchor}, {"baseline_shift", Kind::BaselineShift},
        {"background_color", Kind::BackgroundColor},
        {"background_padding_em", Kind::BackgroundPadding},
        {"background_radius_em", Kind::BackgroundRadius},
        {"stroke_color", Kind::StrokeColor}, {"stroke_width", Kind::StrokeWidth},
        {"character_offset", Kind::CharacterOffset},
        {"word_spacing", Kind::WordSpacing},
        {"line_spacing", Kind::LineSpacing},
        {"glyph_shadow_color", Kind::GlyphShadowColor},
        {"glyph_shadow_offset", Kind::GlyphShadowOffset},
        {"glyph_shadow_blur", Kind::GlyphShadowBlur},
        {"glyph_shadow_opacity", Kind::GlyphShadowOpacity},
        {"glyph_glow_color", Kind::GlyphGlowColor},
        {"glyph_glow_radius", Kind::GlyphGlowRadius},
        {"glyph_glow_intensity", Kind::GlyphGlowIntensity},
        {"glyph_gradient_start_color", Kind::GlyphGradientStartColor},
        {"glyph_gradient_end_color", Kind::GlyphGradientEndColor},
        {"glyph_gradient_angle", Kind::GlyphGradientAngle},
        {"reflow_word_spacing", Kind::ReflowWordSpacing},
        {"reflow_line_spacing", Kind::ReflowLineSpacing},
        {"line_anchor", Kind::LineAnchor},
        {"character_value", Kind::CharacterValue},
        {"character_alignment", Kind::CharacterAlignment},
        {"anchor_grouping", Kind::AnchorGrouping},
        {"light_intensity", Kind::LightIntensity},
        {"light_color", Kind::LightColor},
        {"light_direction", Kind::LightDirection},
    };
    const auto found = kinds.find(name);
    return found == kinds.end() ? Kind::Unknown : found->second;
}

}  // namespace chronon3d
