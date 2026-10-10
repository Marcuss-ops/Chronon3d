#pragma once
#include "enum_mapping.hpp"
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/animation_property_kind.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/render_plan/spring_bake.hpp>
#include <chronon3d/scene/path_sampling.hpp>
#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>
#include <cmath>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
namespace chronon3d::render_plan::detail {
namespace {
using namespace std::literals;

using AnimationPropertyKind = chronon3d::AnimationPropertyKind;
using chronon3d::animation_property_kind;

chronon3d::EasingCurve track_easing(
    std::string_view value,
    const std::optional<std::array<float, 4>>& bezier_curve = std::nullopt,
    const std::optional<render_plan::SpringPlan>& spring = std::nullopt) {
    using chronon3d::Easing;
    if (value == "spring") {
        if (spring) {
            // Spring is baked via sample_spring at evaluation time; the track easing itself stays linear.
            // Per-key spring interpolation is handled in the evaluator.
            return {Easing::Linear};
        }
        throw std::runtime_error("easing='spring' requires a spring object");
    }
    if (spring) throw std::runtime_error("spring object requires easing='spring'");
    if (value == "bezier") {
        if (!bezier_curve)
            throw std::runtime_error("easing='bezier' requires bezier_curve [x1, y1, x2, y2]");
        const auto& curve = *bezier_curve;
        if (!std::isfinite(curve[0]) || !std::isfinite(curve[1]) ||
            !std::isfinite(curve[2]) || !std::isfinite(curve[3]) ||
            curve[0] < 0.0f || curve[0] > 1.0f || curve[2] < 0.0f || curve[2] > 1.0f ||
            curve[1] < -10.0f || curve[1] > 10.0f || curve[3] < -10.0f || curve[3] > 10.0f)
            throw std::runtime_error("bezier_curve handles must be finite; x in [0, 1], y in [-10, 10]");
        return chronon3d::EasingCurve::cubic_bezier(
            curve[0], curve[1], curve[2], curve[3]);
    }
    if (bezier_curve)
        throw std::runtime_error("bezier_curve requires easing='bezier'");
    static constexpr std::array kEasings{
        std::pair{"linear"sv, Easing::Linear},
        std::pair{"in_quad"sv, Easing::InQuad},
        std::pair{"out_quad"sv, Easing::OutQuad},
        std::pair{"in_out_quad"sv, Easing::InOutQuad},
        std::pair{"in_cubic"sv, Easing::InCubic},
        std::pair{"out_cubic"sv, Easing::OutCubic},
        std::pair{"in_out_cubic"sv, Easing::InOutCubic},
        std::pair{"in_expo"sv, Easing::InExpo},
        std::pair{"out_expo"sv, Easing::OutExpo},
        std::pair{"in_out_expo"sv, Easing::InOutExpo},
        std::pair{"in_sine"sv, Easing::InSine},
        std::pair{"out_sine"sv, Easing::OutSine},
        std::pair{"in_out_sine"sv, Easing::InOutSine},
        std::pair{"in_back"sv, Easing::InBack},
        std::pair{"out_back"sv, Easing::OutBack},
        std::pair{"in_out_back"sv, Easing::InOutBack},
        std::pair{"in_elastic"sv, Easing::InElastic},
        std::pair{"out_elastic"sv, Easing::OutElastic},
        std::pair{"in_out_elastic"sv, Easing::InOutElastic},
        std::pair{"in_bounce"sv, Easing::InBounce},
        std::pair{"out_bounce"sv, Easing::OutBounce},
        std::pair{"in_out_bounce"sv, Easing::InOutBounce},
        std::pair{"smoothstep"sv, Easing::Smoothstep},
        std::pair{"hold"sv, Easing::Hold},
    };
    return {enum_from_string(value, kEasings, "unsupported animation easing: ")};
}

chronon3d::EasingCurve keyframe_easing(const AnimationTrackPlan& track,
                                       const AnimationKeyframePlan& key) {
    if (key.spring || track.spring) {
        // Spring tracks use linear segment easing; the spring physics is applied at evaluation.
        if (key.spring && key.easing && *key.easing != "spring") throw std::runtime_error("per-key spring requires easing='spring'");
        if (key.spring && key.bezier_curve) throw std::runtime_error("per-key spring cannot be combined with bezier_curve");
        return {chronon3d::Easing::Linear};
    }
    if (key.interpolation) {
        if (key.easing || (key.bezier_curve && *key.interpolation != "bezier") ||
            (key.bezier_curve && (key.temporal_in_dx || key.temporal_in_dy ||
                                  key.temporal_out_dx || key.temporal_out_dy)))
            throw std::runtime_error("keyframe interpolation cannot be combined with easing or an unrelated bezier_curve");
        if (*key.interpolation == "linear") return {chronon3d::Easing::Linear};
        if (*key.interpolation == "hold") return {chronon3d::Easing::Hold};
        if (*key.interpolation == "bezier") {
            if (!key.bezier_curve &&
                !(key.temporal_in_dx || key.temporal_in_dy ||
                  key.temporal_out_dx || key.temporal_out_dy))
                throw std::runtime_error("keyframe interpolation='bezier' requires bezier_curve or temporal handles");
            return key.bezier_curve ? track_easing("bezier", key.bezier_curve)
                                    : chronon3d::EasingCurve{chronon3d::Easing::Linear};
        }
        if (*key.interpolation == "auto_bezier")
            return chronon3d::EasingCurve{chronon3d::Easing::Linear};
        throw std::runtime_error("unsupported keyframe interpolation: " + *key.interpolation);
    }
    if (key.easing || key.bezier_curve || key.spring) {
        if (key.bezier_curve && !key.easing)
            throw std::runtime_error("per-key bezier_curve requires easing='bezier'");
        if (key.easing == "spring") return {chronon3d::Easing::Linear};
        return track_easing(key.easing.value_or(track.easing),
                            key.bezier_curve ? key.bezier_curve : track.bezier_curve,
                            key.spring ? key.spring : track.spring);
    }
    if (track.easing == "spring") return {chronon3d::Easing::Linear};
    return track_easing(track.easing, track.bezier_curve);
}

float scalar_value(const AnimationKeyframePlan& key, std::string_view property);
chronon3d::Vec3 vector_value(const AnimationKeyframePlan& key,
                             std::string_view property,
                             chronon3d::Vec3 fallback);

float scalar_value(const AnimationKeyframePlan& key, std::string_view property) {
    if (key.value.size() != 1)
        throw std::runtime_error("animation property '" + std::string(property) +
                                 "' requires scalar keyframe values");
    return key.value.front();
}

bool is_per_frame_paint_property(std::string_view property) {
    static constexpr std::array<std::string_view, 7> direct{
        "stroke_dash_offset", "stroke_width", "stroke_color", "fill_color",
        "light_intensity", "light_color", "light_direction"};
    if (std::find(direct.begin(), direct.end(), property) != direct.end()) return true;
    if (!property.starts_with("shadow_")) return false;
    const auto suffix = property.find('_', 7);
    if (suffix == std::string_view::npos) return false;
    const auto index = property.substr(7, suffix - 7);
    if (index != "0" && index != "1" && index != "2") return false;
    const auto field = property.substr(suffix + 1);
    return field == "opacity" || field == "blur" || field == "offset_x" ||
           field == "offset_y" || field == "color";
}

chronon3d::LoopMode loop_mode_value(std::string_view value) {
    static constexpr std::array kLoopModes{
        std::pair{"cycle"sv, chronon3d::LoopMode::Loop},
        std::pair{"pingpong"sv, chronon3d::LoopMode::PingPong},
        std::pair{"offset"sv, chronon3d::LoopMode::Offset},
    };
    return find_enum_value(value, kLoopModes).value_or(chronon3d::LoopMode::Hold);
}

template <typename T>
void apply_data_feed(chronon3d::AnimatedValue<T>& animated,
                     const AnimationTrackPlan& track,
                     const AnimationKeyframePlan& key) {
    if (!track.data_feed) return;
    if constexpr (!std::is_same_v<T, float>) {
        throw std::runtime_error("data_feed requires a scalar animated value");
    } else if (&key == &track.keyframes.front()) {
        const auto values = std::make_shared<const std::vector<float>>(track.data_feed->values);
        const auto offset = track.data_feed->index_offset;
        const auto interpolation = track.data_feed->interpolation;
        animated.frame_evaluator([values, offset, interpolation](double frame) {
            if (!std::isfinite(frame))
                throw std::runtime_error("data_feed cannot sample a non-finite frame");
            const double index = std::clamp(frame + static_cast<double>(offset),
                0.0, static_cast<double>(values->size() - 1));
            const auto lower = static_cast<std::size_t>(std::floor(index));
            if (interpolation == "step" || lower + 1 >= values->size())
                return (*values)[lower];
            const float fraction = static_cast<float>(index - static_cast<double>(lower));
            return (*values)[lower] + ((*values)[lower + 1] - (*values)[lower]) * fraction;
        });
    }
}

template <typename T>
void apply_scalar_driver(chronon3d::AnimatedValue<T>& animated,
                         const AnimationTrackPlan& track) {
    if (!track.oscillate && !track.random_hold && !track.posterize_time) return;
    if constexpr (!std::is_same_v<T, float>) {
        throw std::runtime_error("procedural animation drivers require a scalar animated value");
    } else {
        chronon3d::ScalarAnimationDriver driver;
        if (track.oscillate) {
            driver.kind = chronon3d::ScalarAnimationDriver::Kind::Oscillate;
            driver.frequency_hz = track.oscillate->frequency_hz;
            driver.amplitude = track.oscillate->amplitude;
            driver.phase_deg = track.oscillate->phase_deg;
            driver.offset = track.oscillate->offset;
            if (track.oscillate->waveform == "triangle")
                driver.waveform = chronon3d::ScalarAnimationDriver::Waveform::Triangle;
            else if (track.oscillate->waveform == "sawtooth")
                driver.waveform = chronon3d::ScalarAnimationDriver::Waveform::Sawtooth;
            else if (track.oscillate->waveform == "square")
                driver.waveform = chronon3d::ScalarAnimationDriver::Waveform::Square;
        } else if (track.random_hold) {
            driver.kind = chronon3d::ScalarAnimationDriver::Kind::RandomHold;
            driver.interval_frames = track.random_hold->interval_frames;
            driver.min = track.random_hold->min;
            driver.max = track.random_hold->max;
            driver.seed = track.random_hold->seed;
        } else {
            driver.kind = chronon3d::ScalarAnimationDriver::Kind::PosterizeTime;
            driver.fps = track.posterize_time->fps;
        }
        animated.scalar_driver(driver);
    }
}

template <typename T>
void apply_wiggle(chronon3d::AnimatedValue<T>& animated,
                  const AnimationTrackPlan& track) {
    if (!track.wiggle) return;
    if constexpr (!std::is_same_v<T, float>) {
        throw std::runtime_error("wiggle driver requires a scalar animated value");
    } else {
        animated.expression("value + wiggle(" + std::to_string(track.wiggle->frequency) +
            "," + std::to_string(track.wiggle->amplitude) +
            ",time," + std::to_string(track.wiggle->seed) + ")");
    }
}

template <typename T>
bool bake_spring_segment(chronon3d::AnimatedValue<T>& animated,
                         const AnimationTrackPlan& track,
                         const AnimationKeyframePlan& key,
                         const T& value,
                         FrameRate fps) {
    const auto key_it = std::find_if(track.keyframes.begin(), track.keyframes.end(),
        [&](const AnimationKeyframePlan& candidate) { return candidate.frame == key.frame; });
    const bool is_segment = key_it != track.keyframes.end() &&
        std::next(key_it) != track.keyframes.end() &&
        (key.easing == "spring" || (!key.interpolation && !key.easing && track.easing == "spring"));
    if (!is_segment) return false;

    const auto& next_key = *std::next(key_it);
    const auto spring = key.spring ? key.spring : track.spring;
    if (!spring) throw std::runtime_error("spring segment requires a spring config");
    const auto frame_span = next_key.frame.integral() - key.frame.integral();
    if (frame_span <= 0) throw std::runtime_error("spring segment requires positive frame span");
    const double seconds = static_cast<double>(frame_span) *
        static_cast<double>(fps.den()) / static_cast<double>(fps.num());
    const auto progress = bake_spring_progress(
        *spring, seconds, static_cast<std::uint32_t>(fps.num()),
        static_cast<std::uint32_t>(fps.den()), 1);

    T target{};
    if constexpr (std::is_same_v<T, float>) {
        target = scalar_value(next_key, track.property);
    } else if constexpr (std::is_same_v<T, chronon3d::Color>) {
        if (next_key.value.size() != 4)
            throw std::runtime_error("spring color keyframes require RGBA values");
        target = chronon3d::Color{next_key.value[0], next_key.value[1],
                                  next_key.value[2], next_key.value[3]};
    } else if constexpr (is_glm_vec_type_v<T>) {
        chronon3d::Vec3 target_vec = value;
        if (track.property == "position_x" || track.property == "scale_x" ||
            track.property == "rotation_x") target_vec.x = scalar_value(next_key, track.property);
        else if (track.property == "position_y" || track.property == "scale_y" ||
                 track.property == "rotation_y") target_vec.y = scalar_value(next_key, track.property);
        else if (track.property == "position_z" || track.property == "scale_z" ||
                 track.property == "rotation_z") target_vec.z = scalar_value(next_key, track.property);
        else if (track.property == "rotation" && next_key.value.size() == 1)
            target_vec.z = scalar_value(next_key, track.property);
        else
            target_vec = vector_value(next_key, track.property, value);
        target = T{target_vec};
    } else {
        throw std::runtime_error("spring easing is unsupported for this animation value type");
    }

    animated.add_keyframe(key.frame, value, chronon3d::EasingCurve{chronon3d::Easing::Linear});
    const auto start_frame = key.frame.integral();
    for (std::size_t sample = 1; sample + 1 < progress.size(); ++sample) {
        const auto sample_frame = start_frame + static_cast<std::int64_t>(sample);
        animated.add_keyframe(Frame{sample_frame},
            chronon3d::lerp(value, target, progress[sample]),
            chronon3d::EasingCurve{chronon3d::Easing::Linear});
    }
    return true;
}

template <typename T>
void add_interpolated_keyframe(chronon3d::AnimatedValue<T>& animated,
                               const AnimationTrackPlan& track,
                               const AnimationKeyframePlan& key,
                               const T& value) {
    if (key.interpolation == "bezier" && key.bezier_curve) {
        animated.add_keyframe(key.frame, value, keyframe_easing(track, key), key.roving);
    } else if (key.interpolation == "bezier") {
        if constexpr (is_glm_vec_type_v<T>) {
            animated.add_vector_temporal_keyframe(key.frame, value, chronon3d::InterpMode::Bezier,
                key.temporal_in_dx.value_or(0.0f), T{key.temporal_in_dy.value_or(0.0f)},
                key.temporal_out_dx.value_or(0.0f), T{key.temporal_out_dy.value_or(0.0f)},
                key.roving, chronon3d::EasingCurve{chronon3d::Easing::Linear});
        } else {
            animated.add_keyframe(key.frame, value, chronon3d::InterpMode::Bezier,
                key.temporal_in_dx.value_or(0.0f), key.temporal_in_dy.value_or(0.0f),
                key.temporal_out_dx.value_or(0.0f), key.temporal_out_dy.value_or(0.0f),
                key.roving, chronon3d::EasingCurve{chronon3d::Easing::Linear});
        }
    } else if (key.interpolation == "auto_bezier") {
        animated.add_keyframe(key.frame, value, chronon3d::InterpMode::AutoBezier,
            0.0f, 0.0f, 0.0f, 0.0f, key.roving);
    } else if (key.temporal_in_dx || key.temporal_in_dy ||
               key.temporal_out_dx || key.temporal_out_dy) {
        animated.add_keyframe(key.frame, value, chronon3d::InterpMode::Bezier,
            key.temporal_in_dx.value_or(0.0f), key.temporal_in_dy.value_or(0.0f),
            key.temporal_out_dx.value_or(0.0f), key.temporal_out_dy.value_or(0.0f),
            key.roving, chronon3d::EasingCurve{chronon3d::Easing::Linear});
    } else {
        animated.add_keyframe(key.frame, value, keyframe_easing(track, key), key.roving);
    }
}

template <typename T>
void add_animation_keyframe(chronon3d::AnimatedValue<T>& animated,
                            const AnimationTrackPlan& track,
                            const AnimationKeyframePlan& key,
                            const T& value,
                            FrameRate fps = FrameRate{30, 1}) {
    animated.loop_mode(loop_mode_value(track.loop_mode));
    apply_data_feed(animated, track, key);
    apply_scalar_driver(animated, track);
    apply_wiggle(animated, track);
    if (bake_spring_segment(animated, track, key, value, fps)) return;
    add_interpolated_keyframe(animated, track, key, value);
}

chronon3d::Vec3 vector_value(const AnimationKeyframePlan& key,
                             std::string_view property,
                             chronon3d::Vec3 fallback) {
    if (key.value.size() == 1 && property == "scale")
        return chronon3d::Vec3{key.value[0], key.value[0], key.value[0]};
    if (key.value.size() < 2 || key.value.size() > 3)
        throw std::runtime_error("animation property '" + std::string(property) +
                                 "' requires 2 or 3 keyframe components");
    fallback.x = key.value[0];
    fallback.y = key.value[1];
    if (key.value.size() == 3) fallback.z = key.value[2];
    return fallback;
}

struct PositionAnimationData {
    struct Keyframe {
        Frame frame;
        chronon3d::Vec3 value;
        AnimationKeyframePlan timing;
        chronon3d::Vec3 temporal_in_value{0.0f};
        chronon3d::Vec3 temporal_out_value{0.0f};
        chronon3d::EasingCurve easing;
    };
    AnimationTrackPlan timing;
    std::vector<Keyframe> keyframes;
};

bool has_authored_timing(const AnimationTrackPlan& track) {
    return track.easing != "linear" || track.bezier_curve ||
        std::any_of(track.keyframes.begin(), track.keyframes.end(), [](const auto& key) {
            return key.interpolation || key.easing || key.bezier_curve || key.roving ||
                   key.temporal_in_dx || key.temporal_in_dy ||
                   key.temporal_out_dx || key.temporal_out_dy;
        });
}

const AnimationTrackPlan* verify_shared_timing(
    const std::array<const AnimationTrackPlan*, 3>& components) {
    const AnimationTrackPlan* reference = nullptr;
    for (const auto* component : components) {
        if (component && has_authored_timing(*component)) {
            reference = component;
            break;
        }
    }
    if (!reference) {
        for (const auto* component : components) {
            if (component) {
                reference = component;
                break;
            }
        }
    }
    if (!reference) return nullptr;

    for (const auto* component : components) {
        if (!component || component == reference) continue;
        if (component->easing != reference->easing ||
            component->bezier_curve != reference->bezier_curve ||
            component->keyframes.size() != reference->keyframes.size())
            throw std::runtime_error(
                "position component tracks must share keyframes and easing");
        for (std::size_t index = 0; index < reference->keyframes.size(); ++index) {
            const auto& key = component->keyframes[index];
            const auto& ref_key = reference->keyframes[index];
            if (key.frame != ref_key.frame || key.interpolation != ref_key.interpolation ||
                key.easing != ref_key.easing || key.bezier_curve != ref_key.bezier_curve ||
                key.roving != ref_key.roving || key.temporal_in_dx != ref_key.temporal_in_dx ||
                key.temporal_out_dx != ref_key.temporal_out_dx ||
                key.temporal_in_dy.has_value() != ref_key.temporal_in_dy.has_value() ||
                key.temporal_out_dy.has_value() != ref_key.temporal_out_dy.has_value())
                throw std::runtime_error(
                    "position component tracks must share keyframe timing data");
        }
    }
    return reference;
}

PositionAnimationData position_animation_data(const LayerPlan& layer,
                                               chronon3d::Vec3 base) {
    PositionAnimationData result;
    if (!layer.animation) return result;
    const AnimationTrackPlan* vector_track = nullptr;
    std::array<const AnimationTrackPlan*, 3> component_tracks{};
    const auto component_index = [](std::string_view property) -> int {
        if (property == "position_x") return 0;
        if (property == "position_y") return 1;
        if (property == "position_z") return 2;
        return -1;
    };
    for (const auto& track : layer.animation->tracks) {
        if (track.property == "position") {
            if (vector_track) throw std::runtime_error("layer has overlapping position tracks");
            vector_track = &track;
        } else if (const int component = component_index(track.property); component >= 0) {
            auto*& slot = component_tracks[static_cast<std::size_t>(component)];
            if (slot) throw std::runtime_error("layer has overlapping position component tracks");
            slot = &track;
        }
    }
    const auto component_count = static_cast<std::size_t>(std::count_if(
        component_tracks.begin(), component_tracks.end(), [](const auto* value) { return value; }));
    if (vector_track && component_count != 0)
        throw std::runtime_error("layer cannot combine vector and component position tracks");
    if (!vector_track && component_count == 0) return result;
    const AnimationTrackPlan* verified_components = nullptr;
    if (!vector_track) {
        verified_components = verify_shared_timing(component_tracks);
        if (verified_components && has_authored_timing(*verified_components))
            vector_track = verified_components;
    }
    if (!vector_track && component_count == 0) return result;

    const AnimationTrackPlan* reference = vector_track;
    if (!reference) reference = verified_components;
    result.timing.property = "position";
    result.timing.easing = reference->easing;
    result.timing.bezier_curve = reference->bezier_curve;
    result.timing.spring = reference->spring;
    const auto& reference_keys = reference->keyframes;
    result.keyframes.reserve(reference_keys.size());
    for (std::size_t index = 0; index < reference_keys.size(); ++index) {
        auto value = base;
        auto timing = reference_keys[index];
        chronon3d::Vec3 temporal_in_value{0.0f};
        chronon3d::Vec3 temporal_out_value{0.0f};
        if (vector_track) {
            value = vector_value(reference_keys[index], "position", base);
            if (timing.temporal_in_dy) temporal_in_value = chronon3d::Vec3{*timing.temporal_in_dy};
            if (timing.temporal_out_dy) temporal_out_value = chronon3d::Vec3{*timing.temporal_out_dy};
        } else {
            for (std::size_t component = 0; component < component_tracks.size(); ++component) {
                const auto* track = component_tracks[component];
                if (!track) continue;
                const auto& key = track->keyframes[index];
                const float offset = scalar_value(key, track->property);
                auto& value_axis = component == 0 ? value.x : (component == 1 ? value.y : value.z);
                value_axis += offset;
                auto& in_axis = component == 0 ? temporal_in_value.x :
                                (component == 1 ? temporal_in_value.y : temporal_in_value.z);
                auto& out_axis = component == 0 ? temporal_out_value.x :
                                 (component == 1 ? temporal_out_value.y : temporal_out_value.z);
                if (key.temporal_in_dy) in_axis = *key.temporal_in_dy;
                if (key.temporal_out_dy) out_axis = *key.temporal_out_dy;
            }
        }
        timing.value = {value.x, value.y, value.z};
        result.timing.keyframes.push_back(timing);
        const auto segment_easing = keyframe_easing(*reference, reference_keys[index]);
        result.keyframes.push_back({reference_keys[index].frame, value, std::move(timing),
                                    temporal_in_value, temporal_out_value, segment_easing});
    }
    return result;
}


}
}
