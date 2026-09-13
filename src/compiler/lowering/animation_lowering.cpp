#include "../compiler_stages.hpp"

#include <string_view>

namespace chronon3d::compiler::stages {
namespace {
AnimationProperty property_kind(std::string_view property) {
    if (property == "opacity") return AnimationProperty::Opacity;
    if (property == "position.x") return AnimationProperty::PositionX;
    if (property == "position.y") return AnimationProperty::PositionY;
    if (property == "position.z") return AnimationProperty::PositionZ;
    if (property == "scale.x") return AnimationProperty::ScaleX;
    if (property == "scale.y") return AnimationProperty::ScaleY;
    if (property == "scale.z") return AnimationProperty::ScaleZ;
    return AnimationProperty::Unknown;
}
}

void lower_animation(const render_plan::RenderPlan& plan, CompiledProgramData& out) {
    for (std::uint32_t i = 0; i < plan.layers.size(); ++i) {
        const auto& layer = plan.layers[i];
        if (layer.animation || !layer.text_animators.empty()) {
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
                }
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
