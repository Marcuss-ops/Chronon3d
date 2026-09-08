#include "../compiler_stages.hpp"

namespace chronon3d::compiler::stages {
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
                }
            }
            out.animations.push_back({
                i, first_track,
                static_cast<std::uint32_t>(out.animation_tracks.size() - first_track),
                i});
            out.parameters.push_back({i, i, 1});
        }
    }
}
}
