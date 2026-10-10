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
        {"light_intensity", Kind::LightIntensity},
        {"light_color", Kind::LightColor},
        {"light_direction", Kind::LightDirection},
    };
    const auto found = kinds.find(name);
    return found == kinds.end() ? Kind::Unknown : found->second;
}

}  // namespace chronon3d
