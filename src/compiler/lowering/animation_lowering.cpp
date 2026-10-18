#include "../compiler_stages.hpp"

#include <string_view>
#include <stdexcept>

namespace chronon3d::compiler::stages {
namespace {
bool affects_transform(AnimationProperty property) noexcept {
    switch (property) {
    case AnimationProperty::PositionX:
    case AnimationProperty::PositionY:
    case AnimationProperty::PositionZ:
    case AnimationProperty::PositionXOffset:
    case AnimationProperty::PositionYOffset:
    case AnimationProperty::PositionZOffset:
    case AnimationProperty::ScaleX:
    case AnimationProperty::ScaleY:
    case AnimationProperty::ScaleZ:
    case AnimationProperty::UniformScale:
    case AnimationProperty::RotationX:
    case AnimationProperty::RotationY:
    case AnimationProperty::RotationZ:
        return true;
    default:
        return false;
    }
}

AnimationProperty property_kind(std::string_view property) {
    using Kind = chronon3d::AnimationPropertyKind;
    const auto kind = chronon3d::animation_property_kind(property);
    switch (kind) {
    case Kind::Opacity:
    case Kind::PositionX:
    case Kind::PositionY:
    case Kind::PositionZ:
    case Kind::PositionXOffset:
    case Kind::PositionYOffset:
    case Kind::PositionZOffset:
    case Kind::ScaleX:
    case Kind::ScaleY:
    case Kind::ScaleZ:
    case Kind::UniformScale:
    case Kind::RotationX:
    case Kind::RotationY:
    case Kind::RotationZ:
    case Kind::TimeRemap:
        return kind;
    default:
        return AnimationProperty::Unknown;
    }
}
}

void lower_animation(const render_plan::RenderPlan& plan, CompiledProgramData& out) {
    for (std::uint32_t i = 0; i < plan.layers.size(); ++i) {
        const auto& layer = plan.layers[i];
        if (layer.animation || !layer.text_animators.empty() ||
            (layer.time_remap && layer.time_remap->animation)) {
            const auto first_track = static_cast<std::uint32_t>(out.animation_tracks.size());
            if (layer.animation) {
                for (const auto& track : layer.animation->tracks) {
                    const auto first_keyframe = static_cast<std::uint32_t>(out.curve_keyframes.size());
                    for (const auto& keyframe : track.keyframes) {
                        for (const float value : keyframe.value) {
                            out.curve_keyframes.push_back({keyframe.frame.integral(), value});
                        }
                    }
                    out.animation_tracks.push_back({
                        static_cast<std::uint32_t>(out.animation_tracks.size()),
                        track.property,
                        first_keyframe,
                        static_cast<std::uint32_t>(out.curve_keyframes.size() - first_keyframe)});
                    out.animation_tracks.back().property_kind = property_kind(track.property);
                    out.animation_tracks.back().easing = track.easing;
                }
            }
            if (layer.time_remap && layer.time_remap->animation) {
                const auto& track = *layer.time_remap->animation;
                const auto first_keyframe = static_cast<std::uint32_t>(out.curve_keyframes.size());
                for (const auto& keyframe : track.keyframes) {
                    if (keyframe.value.size() != 1)
                        throw std::runtime_error("time_remap keyframes require scalar values");
                    out.curve_keyframes.push_back({keyframe.frame.integral(), keyframe.value.front()});
                }
                out.animation_tracks.push_back({
                    static_cast<std::uint32_t>(out.animation_tracks.size()),
                    "time_remap",
                    first_keyframe,
                    static_cast<std::uint32_t>(out.curve_keyframes.size() - first_keyframe),
                    AnimationProperty::TimeRemap,
                    track.easing});
                out.layers[i].time_remap_track_index =
                    static_cast<std::uint32_t>(out.animation_tracks.size() - 1);
            }
            const auto animation_index = static_cast<std::uint32_t>(out.animations.size());
            out.animations.push_back({
                i, first_track,
                static_cast<std::uint32_t>(out.animation_tracks.size() - first_track),
                i});
            out.layers[i].animation_program_index = animation_index;
            for (std::size_t track_index = first_track;
                 track_index < out.animation_tracks.size(); ++track_index) {
                const auto& track = out.animation_tracks[track_index];
                if (track.keyframe_count != 0 && affects_transform(track.property_kind)) {
                    out.layers[i].has_transform_animation = true;
                    break;
                }
            }
            out.parameters.push_back({i, i, 1});
        }
    }
}
}
