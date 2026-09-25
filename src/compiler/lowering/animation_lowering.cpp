#include "../compiler_stages.hpp"

#include <string_view>
#include <stdexcept>

namespace chronon3d::compiler::stages {
namespace {
AnimationProperty property_kind(std::string_view property) {
    if (property == "opacity") return AnimationProperty::Opacity;
    if (property == "position.x") return AnimationProperty::PositionX;
    if (property == "position.y") return AnimationProperty::PositionY;
    if (property == "position.z") return AnimationProperty::PositionZ;
    if (property == "position_x") return AnimationProperty::PositionXOffset;
    if (property == "position_y") return AnimationProperty::PositionYOffset;
    if (property == "position_z") return AnimationProperty::PositionZOffset;
    if (property == "scale.x") return AnimationProperty::ScaleX;
    if (property == "scale.y") return AnimationProperty::ScaleY;
    if (property == "scale.z") return AnimationProperty::ScaleZ;
    if (property == "scale_x") return AnimationProperty::ScaleX;
    if (property == "scale_y") return AnimationProperty::ScaleY;
    if (property == "scale_z") return AnimationProperty::ScaleZ;
    if (property == "scale") return AnimationProperty::UniformScale;
    if (property == "rotation.x") return AnimationProperty::RotationX;
    if (property == "rotation.y") return AnimationProperty::RotationY;
    if (property == "rotation.z") return AnimationProperty::RotationZ;
    if (property == "rotation_x") return AnimationProperty::RotationX;
    if (property == "rotation_y") return AnimationProperty::RotationY;
    if (property == "rotation_z") return AnimationProperty::RotationZ;
    return AnimationProperty::Unknown;
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
            out.parameters.push_back({i, i, 1});
        }
    }
}
}
