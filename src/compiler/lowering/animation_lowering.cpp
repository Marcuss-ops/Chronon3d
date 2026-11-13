#include "../compiler_stages.hpp"

#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>

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
                // The typed program stores scalar curve channels. A vector
                // `rotation` track was previously flattened into one channel,
                // then ignored by TypedProgramSampler (RotationVector), so the
                // layer kept its static matrix. Split it into the three
                // absolute component tracks the sampler already evaluates.
                const auto append_scalar_track = [&](std::string property,
                                                     AnimationProperty kind,
                                                     const auto& source,
                                                     std::size_t component) {
                    const auto first_keyframe =
                        static_cast<std::uint32_t>(out.curve_keyframes.size());
                    for (const auto& keyframe : source.keyframes)
                        out.curve_keyframes.push_back(
                            {keyframe.frame.integral(), keyframe.value[component]});
                    out.animation_tracks.push_back({
                        static_cast<std::uint32_t>(out.animation_tracks.size()),
                        std::move(property),
                        first_keyframe,
                        static_cast<std::uint32_t>(
                            out.curve_keyframes.size() - first_keyframe),
                        kind,
                        source.easing});
                };

                for (const auto& track : layer.animation->tracks) {
                    const auto kind = animation_property_kind(track.property);
                    if (kind == chronon3d::AnimationPropertyKind::RotationVector) {
                        if (track.keyframes.empty())
                            throw std::runtime_error(
                                "rotation animation requires at least one keyframe");
                        const auto components = track.keyframes.front().value.size();
                        if (components != 1 && components != 3)
                            throw std::runtime_error(
                                "rotation animation requires scalar or 3-component keyframes");
                        for (const auto& keyframe : track.keyframes) {
                            if (keyframe.value.size() != components)
                                throw std::runtime_error(
                                    "rotation animation keyframes must use a consistent component count");
                        }
                        if (components == 1) {
                            append_scalar_track("rotation.z", AnimationProperty::RotationZ,
                                                track, 0);
                        } else {
                            append_scalar_track("rotation.x", AnimationProperty::RotationX,
                                                track, 0);
                            append_scalar_track("rotation.y", AnimationProperty::RotationY,
                                                track, 1);
                            append_scalar_track("rotation.z", AnimationProperty::RotationZ,
                                                track, 2);
                        }
                        continue;
                    }

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
