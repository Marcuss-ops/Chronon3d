#include "render_plan_compiler_detail.hpp"
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
    static constexpr std::array<std::string_view, 4> direct{
        "stroke_dash_offset", "stroke_width", "stroke_color", "fill_color"};
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


chronon3d::TextSelectorUnit text_selector_unit(std::string_view value) {
    using chronon3d::TextSelectorUnit;
    static constexpr std::array kValues{
        std::pair{"glyph"sv, TextSelectorUnit::Glyph},
        std::pair{"grapheme"sv, TextSelectorUnit::Grapheme},
        std::pair{"character"sv, TextSelectorUnit::Character},
        std::pair{"word"sv, TextSelectorUnit::Word},
        std::pair{"line"sv, TextSelectorUnit::Line},
    };
    return enum_from_string(value, kValues, "unsupported text selector unit: ");
}

chronon3d::TextSelectorShape text_selector_shape(std::string_view value) {
    using chronon3d::TextSelectorShape;
    static constexpr std::array kValues{
        std::pair{"square"sv, TextSelectorShape::Square},
        std::pair{"ramp_up"sv, TextSelectorShape::RampUp},
        std::pair{"ramp_down"sv, TextSelectorShape::RampDown},
        std::pair{"triangle"sv, TextSelectorShape::Triangle},
        std::pair{"round"sv, TextSelectorShape::Round},
        std::pair{"smooth"sv, TextSelectorShape::Smooth},
        std::pair{"wiggly"sv, TextSelectorShape::Wiggly},
        std::pair{"noise"sv, TextSelectorShape::Noise},
    };
    return enum_from_string(value, kValues, "unsupported text selector shape: ");
}

chronon3d::TextSelectorOrder text_selector_order(std::string_view value) {
    using chronon3d::TextSelectorOrder;
    static constexpr std::array kValues{
        std::pair{"forward"sv, TextSelectorOrder::Forward},
        std::pair{"reverse"sv, TextSelectorOrder::Reverse},
        std::pair{"from_center"sv, TextSelectorOrder::FromCenter},
        std::pair{"to_center"sv, TextSelectorOrder::ToCenter},
        std::pair{"random"sv, TextSelectorOrder::Random},
        std::pair{"index"sv, TextSelectorOrder::Index},
    };
    return enum_from_string(value, kValues, "unsupported text selector order: ");
}

chronon3d::SelectorCombineMode text_selector_combine(std::string_view value) {
    using chronon3d::SelectorCombineMode;
    static constexpr std::array kValues{
        std::pair{"replace"sv, SelectorCombineMode::Replace},
        std::pair{"add"sv, SelectorCombineMode::Add},
        std::pair{"subtract"sv, SelectorCombineMode::Subtract},
        std::pair{"intersect"sv, SelectorCombineMode::Intersect},
        std::pair{"min"sv, SelectorCombineMode::Min},
        std::pair{"max"sv, SelectorCombineMode::Max},
    };
    return enum_from_string(value, kValues, "unsupported text selector combine mode: ");
}

template <typename T>
chronon3d::AnimatedValue<T> animated_text_value(
    const AnimationTrackPlan& track, T fallback, std::string_view property,
    FrameRate fps = FrameRate{30, 1}) {
    chronon3d::AnimatedValue<T> result{fallback};
    for (const auto& key : track.keyframes) {
        if constexpr (std::is_same_v<T, float>) {
            add_animation_keyframe(result, track, key, scalar_value(key, property), fps);
        } else {
            add_animation_keyframe(result, track, key, vector_value(key, property, fallback), fps);
        }
    }
    result.compute_roving();
    result.compute_auto_beziers();
    return result;
}

// `rotation` keyframes are scalar (Z rotation in degrees) or full XYZ triples.
chronon3d::AnimatedValue<chronon3d::Vec3> animated_text_rotation(
    const AnimationTrackPlan& track, FrameRate fps) {
    chronon3d::AnimatedValue<chronon3d::Vec3> result{chronon3d::Vec3{}};
    for (const auto& key : track.keyframes) {
        chronon3d::Vec3 value{};
        if (key.value.size() == 1) {
            value.z = scalar_value(key, "rotation");
        } else if (key.value.size() == 3) {
            value = vector_value(key, "rotation", chronon3d::Vec3{});
        } else {
            throw std::runtime_error(
                "animation property 'rotation' requires scalar or 3-component keyframe values");
        }
        add_animation_keyframe(result, track, key, value, fps);
    }
    result.compute_roving();
    result.compute_auto_beziers();
    return result;
}

// `fill_color` keyframes are always RGBA quadruples in 0..1.
chronon3d::AnimatedValue<chronon3d::Color> animated_text_color(
    const AnimationTrackPlan& track, std::string_view property, FrameRate fps,
    chronon3d::Color fallback = chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f}) {
    chronon3d::AnimatedValue<chronon3d::Color> result{
        fallback};
    for (const auto& key : track.keyframes) {
        if (key.value.size() != 4)
            throw std::runtime_error("animation property '" + std::string(property) +
                                     "' requires 4 keyframe components");
        add_animation_keyframe(result, track, key,
            chronon3d::Color{key.value[0], key.value[1], key.value[2], key.value[3]}, fps);
    }
    result.compute_roving();
    result.compute_auto_beziers();
    return result;
}

chronon3d::AnimatedValue<chronon3d::Vec3> animated_text_vec3_component(
    const AnimationTrackPlan& track, chronon3d::Vec3 fallback,
    std::string_view property, FrameRate fps) {
    chronon3d::AnimatedValue<chronon3d::Vec3> result{fallback};
    for (const auto& key : track.keyframes) {
        auto value = fallback;
        if (property == "position_x" || property == "scale_x")
            value.x = scalar_value(key, property);
        else if (property == "position_y" || property == "scale_y")
            value.y = scalar_value(key, property);
        else
            value.z = scalar_value(key, property);
        add_animation_keyframe(result, track, key, value, fps);
    }
    result.compute_roving();
    result.compute_auto_beziers();
    return result;
}

chronon3d::GlyphSelectorSpec compile_text_selector(const TextSelectorPlan& plan, FrameRate fps) {
    chronon3d::GlyphSelectorSpec selector;
    selector.id = plan.id;
    selector.semantic_id = plan.semantic_id;
    selector.unit = text_selector_unit(plan.unit);
    selector.shape = text_selector_shape(plan.shape);
    selector.order = text_selector_order(plan.order);
    selector.combine = text_selector_combine(plan.combine);
    selector.start = animated_text_value(plan.start, 0.0f, "selector.start", fps);
    selector.end = animated_text_value(plan.end, 100.0f, "selector.end", fps);
    selector.offset = animated_text_value(plan.offset, 0.0f, "selector.offset", fps);
    selector.amount = animated_text_value(plan.amount, 100.0f, "selector.amount", fps);
    selector.exclude_spaces = plan.exclude_spaces;
    selector.randomize_order = plan.randomize_order;
    selector.random_seed = plan.random_seed;
    selector.wiggly = chronon3d::WigglySelectorParams{
        plan.wiggly_frequency, plan.wiggly_amplitude, plan.wiggly_seed};
    selector.noise = chronon3d::WigglySelectorParams{
        plan.noise_frequency, plan.noise_amplitude, plan.noise_seed};
    selector.index_step = plan.index_step;
    return selector;
}

chronon3d::TextAnimatorSpec compile_text_animator(const TextAnimatorPlan& plan, FrameRate fps) {
    chronon3d::TextAnimatorSpec animator;
    animator.id = plan.id;
    for (const auto& selector : plan.selectors)
        animator.selectors.push_back(compile_text_selector(selector, fps));
    for (const auto& track : plan.properties) {
        switch (animation_property_kind(track.property)) {
        case AnimationPropertyKind::PositionVector:
        case AnimationPropertyKind::PositionXOffset:
        case AnimationPropertyKind::PositionYOffset:
            animator.properties.push_back(chronon3d::PositionProperty{
                track.property == "position"
                    ? animated_text_value(track, chronon3d::Vec3{}, track.property, fps)
                    : animated_text_vec3_component(track, chronon3d::Vec3{}, track.property, fps)});
            break;
        case AnimationPropertyKind::UniformScale:
        case AnimationPropertyKind::ScaleX:
        case AnimationPropertyKind::ScaleY:
            animator.properties.push_back(chronon3d::ScaleProperty{
                track.property == "scale"
                    ? animated_text_value(track, chronon3d::Vec3{1.0f, 1.0f, 1.0f}, track.property, fps)
                    : animated_text_vec3_component(
                          track, chronon3d::Vec3{1.0f, 1.0f, 1.0f}, track.property, fps)});
            break;
        case AnimationPropertyKind::Opacity:
            animator.properties.push_back(chronon3d::OpacityProperty{
                animated_text_value(track, 1.0f, track.property, fps)});
            break;
        case AnimationPropertyKind::Blur:
            animator.properties.push_back(chronon3d::BlurProperty{
                animated_text_value(track, 0.0f, track.property, fps)});
            break;
        case AnimationPropertyKind::Tracking:
            animator.properties.push_back(chronon3d::TrackingProperty{
                animated_text_value(track, 0.0f, track.property, fps)});
            break;
        case AnimationPropertyKind::RotationVector:
            animator.properties.push_back(chronon3d::RotationProperty{
                animated_text_rotation(track, fps)});
            break;
        case AnimationPropertyKind::FillColor:
            animator.properties.push_back(chronon3d::FillColorProperty{
                animated_text_color(track, track.property, fps)});
            break;
        case AnimationPropertyKind::Skew:
        case AnimationPropertyKind::SkewAxis: {
            chronon3d::SkewProperty skew;
            skew.animate_angle = animation_property_kind(track.property) == AnimationPropertyKind::Skew;
            skew.animate_axis = animation_property_kind(track.property) == AnimationPropertyKind::SkewAxis;
            if (skew.animate_angle)
                skew.angle = animated_text_value(track, 0.0f, track.property, fps);
            if (skew.animate_axis)
                skew.axis = animated_text_value(track, 0.0f, track.property, fps);
            animator.properties.push_back(std::move(skew));
            break;
        }
        case AnimationPropertyKind::Anchor:
            animator.properties.push_back(chronon3d::AnchorProperty{
                animated_text_value(track, chronon3d::Vec3{}, track.property, fps)});
            break;
        case AnimationPropertyKind::BaselineShift:
            animator.properties.push_back(chronon3d::BaselineShiftProperty{
                animated_text_value(track, 0.0f, track.property, fps)});
            break;
        case AnimationPropertyKind::BackgroundColor: {
            chronon3d::BackgroundColorProperty background;
            background.color = animated_text_color(track, track.property, fps,
                chronon3d::Color{0.0f, 0.0f, 0.0f, 0.0f});
            background.animate_padding = false;
            background.animate_radius = false;
            animator.properties.push_back(std::move(background));
            break;
        }
        case AnimationPropertyKind::BackgroundPadding: {
            chronon3d::BackgroundColorProperty background;
            background.animate_color = false;
            background.animate_radius = false;
            background.padding_em = animated_text_value(track, 0.5f, track.property, fps);
            animator.properties.push_back(std::move(background));
            break;
        }
        case AnimationPropertyKind::BackgroundRadius: {
            chronon3d::BackgroundColorProperty background;
            background.animate_color = false;
            background.animate_padding = false;
            background.radius_em = animated_text_value(track, 0.0f, track.property, fps);
            animator.properties.push_back(std::move(background));
            break;
        }
        case AnimationPropertyKind::StrokeColor:
            animator.properties.push_back(chronon3d::StrokeColorProperty{
                animated_text_color(track, track.property, fps,
                    chronon3d::Color{0.0f, 0.0f, 0.0f, 1.0f})});
            break;
        case AnimationPropertyKind::StrokeWidth:
            animator.properties.push_back(chronon3d::StrokeWidthProperty{
                animated_text_value(track, 0.0f, track.property, fps)});
            break;
        default:
            throw std::runtime_error("unsupported text animator property: " + track.property);
        }
    }
    if (animator.selectors.empty() || animator.properties.empty())
        throw std::runtime_error("text animator '" + animator.id + "' requires selectors and properties");
    return animator;
}

void apply_animation_tracks(chronon3d::LayerBuilder& builder,
                            const LayerPlan& layer,
                            int canvas_width,
                            int canvas_height,
                            FrameRate fps,
                            const FollowPathEvaluators* precompiled_follow_path) {
    if (!layer.animation && !layer.follow_path) return;
    chronon3d::Vec3 base_position{layer.position[0], layer.position[1], layer.position[2]};
    if ((layer.type == LayerType::Text || layer.type == LayerType::Shape ||
         layer.type == LayerType::Particles || layer.type == LayerType::Light) &&
        layer.position_dimensions >= 2) {
        // Primitive animation keyframes are offsets from the authored element
        // centre. Resolve that centre once before applying the offsets.
        base_position.x -= static_cast<float>(canvas_width) * 0.5f;
        base_position.y -= static_cast<float>(canvas_height) * 0.5f;
    }
    const chronon3d::Vec3 base_scale{layer.scale[0], layer.scale[1], layer.scale[2]};
    const chronon3d::Vec3 base_rotation{layer.rotation[0], layer.rotation[1], layer.rotation[2]};

    struct AxisTrackGroup {
        const AnimationTrackPlan* full = nullptr;
        const AnimationTrackPlan* x = nullptr;
        const AnimationTrackPlan* y = nullptr;
        const AnimationTrackPlan* z = nullptr;
    };
    struct SkewTrackGroup {
        const AnimationTrackPlan* angle = nullptr;
        const AnimationTrackPlan* axis = nullptr;
        [[nodiscard]] bool empty() const { return !angle && !axis; }
    };

    AxisTrackGroup pos_tracks;
    AxisTrackGroup scale_tracks;
    AxisTrackGroup rot_tracks;
    const AnimationTrackPlan* opacity_track = nullptr;
    const AnimationTrackPlan* blur_track = nullptr;
    SkewTrackGroup skew_tracks;

    auto assign_track = [&](const AnimationTrackPlan*& slot, const AnimationTrackPlan& track) {
        if (slot)
            throw std::runtime_error("layer '" + layer.id + "' has duplicate " +
                                     track.property + " tracks");
        slot = &track;
    };
    auto assign_axis_track = [&](AxisTrackGroup& group, const AnimationTrackPlan& track,
                                 AnimationPropertyKind kind) {
        switch (kind) {
        case AnimationPropertyKind::PositionVector:
        case AnimationPropertyKind::UniformScale:
        case AnimationPropertyKind::RotationVector:
            assign_track(group.full, track);
            return;
        case AnimationPropertyKind::PositionXOffset:
        case AnimationPropertyKind::ScaleX:
        case AnimationPropertyKind::RotationX:
            assign_track(group.x, track);
            return;
        case AnimationPropertyKind::PositionYOffset:
        case AnimationPropertyKind::ScaleY:
        case AnimationPropertyKind::RotationY:
            assign_track(group.y, track);
            return;
        case AnimationPropertyKind::PositionZOffset:
        case AnimationPropertyKind::ScaleZ:
        case AnimationPropertyKind::RotationZ:
            assign_track(group.z, track);
            return;
        default:
            return;
        }
    };

    if (layer.animation) for (const auto& track : layer.animation->tracks) {
        const auto kind = animation_property_kind(track.property);
        switch (kind) {
        case AnimationPropertyKind::PositionVector:
        case AnimationPropertyKind::PositionXOffset:
        case AnimationPropertyKind::PositionYOffset:
        case AnimationPropertyKind::PositionZOffset:
            assign_axis_track(pos_tracks, track, kind);
            break;
        case AnimationPropertyKind::UniformScale:
        case AnimationPropertyKind::ScaleX:
        case AnimationPropertyKind::ScaleY:
        case AnimationPropertyKind::ScaleZ:
            assign_axis_track(scale_tracks, track, kind);
            break;
        case AnimationPropertyKind::RotationVector:
        case AnimationPropertyKind::RotationX:
        case AnimationPropertyKind::RotationY:
        case AnimationPropertyKind::RotationZ:
            assign_axis_track(rot_tracks, track, kind);
            break;
        case AnimationPropertyKind::Opacity:
            assign_track(opacity_track, track);
            break;
        case AnimationPropertyKind::Blur:
            assign_track(blur_track, track);
            break;
        case AnimationPropertyKind::Skew:
            assign_track(skew_tracks.angle, track);
            break;
        case AnimationPropertyKind::SkewAxis:
            assign_track(skew_tracks.axis, track);
            break;
        case AnimationPropertyKind::FillColor:
            // Paint tracks are sampled into the per-frame source node.
            break;
        case AnimationPropertyKind::StrokeColor:
        case AnimationPropertyKind::StrokeWidth:
            // Shape paint tracks are materialized by the per-frame paint path.
            break;
        default:
            if (is_per_frame_paint_property(track.property)) break;
            throw std::runtime_error("unsupported primitive animation property: " + track.property);
        }
    }

    if (pos_tracks.full && (pos_tracks.x || pos_tracks.y || pos_tracks.z))
        throw std::runtime_error("layer '" + layer.id + "' mixes 'position' with component tracks");
    if (scale_tracks.full && (scale_tracks.x || scale_tracks.y || scale_tracks.z))
        throw std::runtime_error("layer '" + layer.id + "' mixes 'scale' with component tracks");
    if (rot_tracks.full && (rot_tracks.x || rot_tracks.y || rot_tracks.z))
        throw std::runtime_error("layer '" + layer.id + "' mixes 'rotation' with component tracks");

    auto compile_component_track = [&](const AnimationTrackPlan* track, float fallback) {
        chronon3d::AnimatedValue<float> result{fallback};
        if (!track) return result;
        for (const auto& key : track->keyframes)
            add_animation_keyframe(result, *track, key, scalar_value(key, track->property), fps);
        result.compute_roving();
        result.compute_auto_beziers();
        return result;
    };

    auto prepare_composed_component_loops = [&](const AxisTrackGroup& group,
                                                auto& x, auto& y, auto& z,
                                                chronon3d::AnimatedValue<chronon3d::Vec3>& output) {
        const AnimationTrackPlan* reference = nullptr;
        for (const auto* track : {group.x, group.y, group.z}) {
            if (!track) continue;
            if (reference && track->loop_mode != reference->loop_mode)
                throw std::runtime_error("component tracks must share loop_mode");
            reference = track;
        }
        // Sample authored key values without remapping their endpoints. The
        // composed vector receives the loop mode and handles remapping once.
        if (x) x->loop_mode(chronon3d::LoopMode::Hold);
        if (y) y->loop_mode(chronon3d::LoopMode::Hold);
        if (z) z->loop_mode(chronon3d::LoopMode::Hold);
        if (reference) output.loop_mode(loop_mode_value(reference->loop_mode));
    };

    auto dominant_easing = [](const AnimationTrackPlan* x,
                              const AnimationTrackPlan* y,
                              const AnimationTrackPlan* z,
                              Frame f) -> chronon3d::EasingCurve {
        chronon3d::EasingCurve result{chronon3d::Easing::Linear};
        for (const auto* track : {x, y, z}) {
            if (!track) continue;
            for (const auto& key : track->keyframes) {
                if (key.frame != f) continue;
                const auto curve = keyframe_easing(*track, key);
                if (curve != chronon3d::Easing::Linear || result == chronon3d::Easing::Linear)
                    result = curve;
            }
        }
        return result;
    };

    auto add_component_vector_track = [&](chronon3d::AnimatedValue<chronon3d::Vec3>& animated,
                                          const AxisTrackGroup& group,
                                          const chronon3d::Vec3& fallback,
                                          bool additive) {
        const std::array<const AnimationTrackPlan*, 3> components{group.x, group.y, group.z};
        const AnimationTrackPlan* reference = nullptr;
        for (const auto* track : components) {
            if (!track) continue;
            for (const auto& key : track->keyframes)
                if (track->easing != "linear" || track->bezier_curve || key.interpolation ||
                    key.easing || key.bezier_curve || key.roving || key.temporal_in_dx ||
                    key.temporal_in_dy || key.temporal_out_dx || key.temporal_out_dy) {
                    reference = track;
                    break;
                }
            if (reference) break;
        }
        if (!reference) return false;
        if (reference->easing == "spring" || std::any_of(reference->keyframes.begin(),
                reference->keyframes.end(), [](const auto& key) { return key.easing == "spring"; }))
            return false;
        for (const auto* track : components) {
            if (!track) continue;
            if (track->loop_mode != reference->loop_mode)
                throw std::runtime_error("component tracks with per-key timing must share loop_mode");
            if (track->easing != reference->easing || track->bezier_curve != reference->bezier_curve ||
                track->keyframes.size() != reference->keyframes.size())
                throw std::runtime_error("component tracks with per-key timing must share keyframes and track easing");
            for (std::size_t index = 0; index < reference->keyframes.size(); ++index) {
                const auto& key = track->keyframes[index];
                const auto& ref = reference->keyframes[index];
                if (key.frame != ref.frame || key.interpolation != ref.interpolation ||
                    key.easing != ref.easing || key.bezier_curve != ref.bezier_curve ||
                    key.roving != ref.roving || key.temporal_in_dx != ref.temporal_in_dx ||
                    key.temporal_out_dx != ref.temporal_out_dx ||
                    key.temporal_in_dy.has_value() != ref.temporal_in_dy.has_value() ||
                    key.temporal_out_dy.has_value() != ref.temporal_out_dy.has_value())
                    throw std::runtime_error("component tracks with per-key timing must share keyframes and timing data");
            }
        }
        for (std::size_t index = 0; index < reference->keyframes.size(); ++index) {
            auto value = fallback;
            chronon3d::Vec3 in_dy{0.0f};
            chronon3d::Vec3 out_dy{0.0f};
            for (std::size_t axis = 0; axis < components.size(); ++axis) {
                const auto* track = components[axis];
                if (!track) continue;
                const auto scalar = scalar_value(track->keyframes[index], track->property);
                auto& component = axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
                component = additive ? component + scalar : scalar;
                const auto& key = track->keyframes[index];
                if (key.temporal_in_dy) {
                    auto& in_component = axis == 0 ? in_dy.x : (axis == 1 ? in_dy.y : in_dy.z);
                    in_component = *key.temporal_in_dy;
                }
                if (key.temporal_out_dy) {
                    auto& out_component = axis == 0 ? out_dy.x : (axis == 1 ? out_dy.y : out_dy.z);
                    out_component = *key.temporal_out_dy;
                }
            }
            const auto& reference_key = reference->keyframes[index];
            if (reference_key.interpolation == "bezier" && !reference_key.bezier_curve) {
                animated.add_vector_temporal_keyframe(reference_key.frame, value,
                    chronon3d::InterpMode::Bezier,
                    reference_key.temporal_in_dx.value_or(0.0f), in_dy,
                    reference_key.temporal_out_dx.value_or(0.0f), out_dy,
                    reference_key.roving, chronon3d::EasingCurve{chronon3d::Easing::Linear});
            } else {
                AnimationKeyframePlan vector_key = reference_key;
                vector_key.value = {value.x, value.y, value.z};
            add_animation_keyframe(animated, *reference, vector_key, value, fps);
            }
        }
        animated.compute_roving();
        animated.compute_auto_beziers();
        return true;
    };

    if (pos_tracks.full) {
        auto& animated = builder.position_anim();
        animated.set(base_position);
        const bool has_axis_easing = std::any_of(pos_tracks.full->keyframes.begin(),
            pos_tracks.full->keyframes.end(), [](const auto& key) { return key.easing_xy.has_value(); });
        if (has_axis_easing) {
            auto x_track = *pos_tracks.full;
            auto y_track = *pos_tracks.full;
            x_track.property = "position_x";
            y_track.property = "position_y";
            const std::array<std::pair<AnimationTrackPlan*, std::size_t>, 2> axis_tracks{{
                {&x_track, 0}, {&y_track, 1}}};
            for (const auto& [track, axis] : axis_tracks)
                for (std::size_t index = 0; index < track->keyframes.size(); ++index) {
                    auto& key = track->keyframes[index];
                    if (key.value.size() != 2)
                        throw std::runtime_error("easing_xy requires 2D position keyframes");
                    key.value = {key.value[axis]};
                    if (key.easing_xy) key.easing = (*key.easing_xy)[axis];
                    key.easing_xy.reset();
                }
            animated.frame_evaluator([x_track = std::move(x_track),
                                      y_track = std::move(y_track),
                                      base_position, fps](double frame) {
                return chronon3d::Vec3{
                    evaluate_scalar_track(x_track, frame, base_position.x, fps),
                    evaluate_scalar_track(y_track, frame, base_position.y, fps),
                    base_position.z};
            });
        } else {
            for (const auto& key : pos_tracks.full->keyframes)
                add_animation_keyframe(animated, *pos_tracks.full, key,
                    vector_value(key, pos_tracks.full->property, base_position), fps);
        }
        animated.compute_roving();
        animated.compute_auto_beziers();
    } else if (pos_tracks.x || pos_tracks.y || pos_tracks.z) {
        auto& animated = builder.position_anim();
        animated.set(base_position);
        if (add_component_vector_track(animated, pos_tracks, base_position, true)) {
            // Timing metadata was preserved on the composed vector keyframes.
        } else {
        auto eval_x = pos_tracks.x ? std::optional{compile_component_track(pos_tracks.x, 0.0f)} : std::nullopt;
        auto eval_y = pos_tracks.y ? std::optional{compile_component_track(pos_tracks.y, 0.0f)} : std::nullopt;
        auto eval_z = pos_tracks.z ? std::optional{compile_component_track(pos_tracks.z, 0.0f)} : std::nullopt;
        prepare_composed_component_loops(pos_tracks, eval_x, eval_y, eval_z, animated);

        std::set<Frame> frames;
        for (const auto* t : {pos_tracks.x, pos_tracks.y, pos_tracks.z}) {
            if (t) {
                for (const auto& k : t->keyframes) frames.insert(k.frame);
            }
        }
        for (const auto* t : {pos_tracks.x, pos_tracks.y, pos_tracks.z})
            if (t && (t->easing == "spring" || std::any_of(t->keyframes.begin(), t->keyframes.end(),
                    [](const auto& key) { return key.easing == "spring"; })))
                for (Frame f{t->keyframes.front().frame.integral()}; f <= t->keyframes.back().frame; f += Frame{1})
                    frames.insert(f);

        for (const auto& f : frames) {
            chronon3d::Vec3 v = base_position;
            if (eval_x) v.x += eval_x->evaluate(static_cast<double>(f.integral()));
            if (eval_y) v.y += eval_y->evaluate(static_cast<double>(f.integral()));
            if (eval_z) v.z += eval_z->evaluate(static_cast<double>(f.integral()));
            animated.add_keyframe(f, v, dominant_easing(pos_tracks.x, pos_tracks.y, pos_tracks.z, f));
        }
        }
    }

    if (scale_tracks.full) {
        auto& animated = builder.scale_anim();
        animated.set(base_scale);
        for (const auto& key : scale_tracks.full->keyframes)
            add_animation_keyframe(animated, *scale_tracks.full, key,
                vector_value(key, scale_tracks.full->property, base_scale), fps);
        animated.compute_roving();
        animated.compute_auto_beziers();
    } else if (scale_tracks.x || scale_tracks.y || scale_tracks.z) {
        auto& animated = builder.scale_anim();
        animated.set(base_scale);
        if (add_component_vector_track(animated, scale_tracks, base_scale, false)) {
            // Timing metadata was preserved on the composed vector keyframes.
        } else {
        auto eval_x = scale_tracks.x ? std::optional{compile_component_track(scale_tracks.x, base_scale.x)} : std::nullopt;
        auto eval_y = scale_tracks.y ? std::optional{compile_component_track(scale_tracks.y, base_scale.y)} : std::nullopt;
        auto eval_z = scale_tracks.z ? std::optional{compile_component_track(scale_tracks.z, base_scale.z)} : std::nullopt;
        prepare_composed_component_loops(scale_tracks, eval_x, eval_y, eval_z, animated);

        std::set<Frame> frames;
        for (const auto* t : {scale_tracks.x, scale_tracks.y, scale_tracks.z}) {
            if (t) {
                for (const auto& k : t->keyframes) frames.insert(k.frame);
            }
        }
        for (const auto* t : {scale_tracks.x, scale_tracks.y, scale_tracks.z})
            if (t && (t->easing == "spring" || std::any_of(t->keyframes.begin(), t->keyframes.end(),
                    [](const auto& key) { return key.easing == "spring"; })))
                for (Frame f{t->keyframes.front().frame.integral()}; f <= t->keyframes.back().frame; f += Frame{1})
                    frames.insert(f);

        for (const auto& f : frames) {
            chronon3d::Vec3 v = base_scale;
            if (eval_x) v.x = eval_x->evaluate(static_cast<double>(f.integral()));
            if (eval_y) v.y = eval_y->evaluate(static_cast<double>(f.integral()));
            if (eval_z) v.z = eval_z->evaluate(static_cast<double>(f.integral()));
            animated.add_keyframe(f, v, dominant_easing(scale_tracks.x, scale_tracks.y, scale_tracks.z, f));
        }
        }
    }

    if (rot_tracks.full) {
        auto& animated = builder.rotate_anim();
        animated.set(base_rotation);
        for (const auto& key : rot_tracks.full->keyframes)
            add_animation_keyframe(animated, *rot_tracks.full, key,
                vector_value(key, rot_tracks.full->property, base_rotation), fps);
        animated.compute_roving();
        animated.compute_auto_beziers();
    } else if (rot_tracks.x || rot_tracks.y || rot_tracks.z) {
        auto& animated = builder.rotate_anim();
        animated.set(base_rotation);
        if (add_component_vector_track(animated, rot_tracks, base_rotation, false)) {
            // Timing metadata was preserved on the composed vector keyframes.
        } else {
        auto eval_x = rot_tracks.x ? std::optional{compile_component_track(rot_tracks.x, base_rotation.x)} : std::nullopt;
        auto eval_y = rot_tracks.y ? std::optional{compile_component_track(rot_tracks.y, base_rotation.y)} : std::nullopt;
        auto eval_z = rot_tracks.z ? std::optional{compile_component_track(rot_tracks.z, base_rotation.z)} : std::nullopt;
        prepare_composed_component_loops(rot_tracks, eval_x, eval_y, eval_z, animated);

        std::set<Frame> frames;
        for (const auto* t : {rot_tracks.x, rot_tracks.y, rot_tracks.z}) {
            if (t) {
                for (const auto& k : t->keyframes) frames.insert(k.frame);
            }
        }
        for (const auto* t : {rot_tracks.x, rot_tracks.y, rot_tracks.z})
            if (t && (t->easing == "spring" || std::any_of(t->keyframes.begin(), t->keyframes.end(),
                    [](const auto& key) { return key.easing == "spring"; })))
                for (Frame f{t->keyframes.front().frame.integral()}; f <= t->keyframes.back().frame; f += Frame{1})
                    frames.insert(f);

        for (const auto& f : frames) {
            chronon3d::Vec3 v = base_rotation;
            if (eval_x) v.x = eval_x->evaluate(static_cast<double>(f.integral()));
            if (eval_y) v.y = eval_y->evaluate(static_cast<double>(f.integral()));
            if (eval_z) v.z = eval_z->evaluate(static_cast<double>(f.integral()));
            animated.add_keyframe(f, v, dominant_easing(rot_tracks.x, rot_tracks.y, rot_tracks.z, f));
        }
        }
    }

    if (layer.auto_orient) {
        auto& position = builder.position_anim();
        const auto& position_keys = position.keyframes();
        if (position_keys.size() < 2)
            throw std::runtime_error("auto_orient requires at least two evaluated position keyframes");
        const auto first_frame = position_keys.front().frame.integral();
        const auto last_frame = position_keys.back().frame.integral();
        if (last_frame <= first_frame || last_frame - first_frame > 65535)
            throw std::runtime_error("auto_orient position track span must be in [1, 65535] frames");

        auto& rotation = builder.rotate_anim();
        rotation.set(base_rotation);
        rotation.loop_mode(position.loop_mode());
        float last_angle = base_rotation.z;
        constexpr double kTangentSample = 0.5;
        constexpr float kTangentEpsilon = 1.0e-6f;
        for (std::int64_t frame = first_frame; frame <= last_frame; ++frame) {
            const double left_frame = std::max(
                static_cast<double>(first_frame), static_cast<double>(frame) - kTangentSample);
            const double right_frame = std::min(
                static_cast<double>(last_frame), static_cast<double>(frame) + kTangentSample);
            const auto delta = position.evaluate(right_frame) - position.evaluate(left_frame);
            if (std::hypot(delta.x, delta.y) > kTangentEpsilon) {
                float angle = glm::degrees(std::atan2(delta.y, delta.x));
                while (angle - last_angle > 180.0f) angle -= 360.0f;
                while (angle - last_angle < -180.0f) angle += 360.0f;
                last_angle = angle;
            }
            rotation.add_keyframe(Frame{frame},
                chronon3d::Vec3{base_rotation.x, base_rotation.y, last_angle});
        }
        rotation.compute_roving();
        rotation.compute_auto_beziers();
    }

    if (opacity_track) {
        auto& animated = builder.opacity_anim();
        animated.set(layer.opacity.value_or(1.0f));
        for (const auto& key : opacity_track->keyframes)
            add_animation_keyframe(animated, *opacity_track, key,
                scalar_value(key, opacity_track->property), fps);
        animated.compute_roving();
        animated.compute_auto_beziers();
    }
    if (blur_track) {
        auto& animated = builder.blur_anim();
        animated.set(0.0f);
        for (const auto& key : blur_track->keyframes)
            add_animation_keyframe(animated, *blur_track, key,
                scalar_value(key, blur_track->property), fps);
        animated.compute_roving();
        animated.compute_auto_beziers();
    }
    if (!skew_tracks.empty()) {
        auto& animated = builder.skew_anim();
        animated.set(chronon3d::Vec3{layer.skew_angle_deg, layer.skew_axis_deg, 0.0f});
        const auto angle_track = skew_tracks.angle
            ? std::optional<AnimationTrackPlan>{*skew_tracks.angle} : std::nullopt;
        const auto axis_track = skew_tracks.axis
            ? std::optional<AnimationTrackPlan>{*skew_tracks.axis} : std::nullopt;
        const float base_x = layer.skew_angle_deg;
        const float base_y = layer.skew_axis_deg;
        animated.frame_evaluator([angle_track, axis_track, base_x, base_y, fps](double frame) {
            const float angle = std::clamp(
                angle_track ? evaluate_scalar_track(*angle_track, frame, base_x, fps) : base_x,
                -80.0f, 80.0f);
            const float axis = axis_track
                ? evaluate_scalar_track(*axis_track, frame, base_y, fps) : base_y;
            return chronon3d::Vec3{angle, axis, 0.0f};
        });
    }

    if (layer.follow_path) {
        const auto& follow = *layer.follow_path;
        const auto owned_evaluators = precompiled_follow_path
            ? FollowPathEvaluators{}
            : make_follow_path_evaluators(
                  follow, base_position, base_rotation, canvas_width, canvas_height, fps);
        const auto& evaluators = precompiled_follow_path
            ? *precompiled_follow_path : owned_evaluators;

        auto& position = builder.position_anim();
        position.set(base_position);
        position.frame_evaluator(evaluators.position);
        if (follow.auto_orient && !(rot_tracks.full || rot_tracks.x || rot_tracks.y || rot_tracks.z)) {
            auto& rotation = builder.rotate_anim();
            rotation.set(base_rotation);
            rotation.frame_evaluator(evaluators.rotation);
        }
    }
}

}  // namespace

FollowPathEvaluators make_follow_path_evaluators(
    const LayerPlan::FollowPathDriverPlan& follow,
    chronon3d::Vec3 base_position,
    chronon3d::Vec3 base_rotation,
    int canvas_width,
    int canvas_height,
    FrameRate fps) {
    std::vector<chronon3d::scene::PathSampleCommand> commands;
    commands.reserve(follow.path_commands.size());
    for (const auto& command : follow.path_commands) {
        chronon3d::scene::PathSampleCommand sample;
        switch (command.type) {
            case ShapePathCommandTypePlan::MoveTo:
                sample.type = chronon3d::scene::PathSampleCommandType::MoveTo; break;
            case ShapePathCommandTypePlan::LineTo:
                sample.type = chronon3d::scene::PathSampleCommandType::LineTo; break;
            case ShapePathCommandTypePlan::QuadraticTo:
                sample.type = chronon3d::scene::PathSampleCommandType::QuadraticTo; break;
            case ShapePathCommandTypePlan::CubicTo:
                sample.type = chronon3d::scene::PathSampleCommandType::CubicTo; break;
            case ShapePathCommandTypePlan::Close:
                sample.type = chronon3d::scene::PathSampleCommandType::Close; break;
        }
        sample.point = {command.point[0], command.point[1]};
        sample.control1 = {command.control1[0], command.control1[1]};
        sample.control2 = {command.control2[0], command.control2[1]};
        commands.push_back(sample);
    }
    auto table = std::make_shared<const chronon3d::scene::PathArcLengthTable>(commands);
    chronon3d::AnimatedValue<float> progress{0.0f};
    for (const auto& key : follow.progress.keyframes)
        add_animation_keyframe(progress, follow.progress, key, key.value.front(), fps);
    progress.loop_mode(loop_mode_value(follow.progress.loop_mode));
    progress.compute_roving();
    progress.compute_auto_beziers();

    FollowPathEvaluators result;
    result.position = [table, progress, clamp = follow.clamp_end,
                       canvas_width, canvas_height, z = base_position.z, fps](double frame) {
        const float u = progress.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
        const auto point = table->point_at(u, clamp);
        return chronon3d::Vec3{point.x - static_cast<float>(canvas_width) * 0.5f,
                               point.y - static_cast<float>(canvas_height) * 0.5f, z};
    };
    if (follow.auto_orient) {
        result.rotation = [table, progress, offset = follow.offset_deg, base_rotation,
                           fps](double frame) {
            const float u = progress.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
            const auto tangent = table->tangent_at(u);
            const float angle = glm::degrees(std::atan2(tangent.y, tangent.x)) + offset;
            return chronon3d::Vec3{base_rotation.x, base_rotation.y, angle};
        };
    }
    return result;
}

std::optional<float> stroke_dash_offset_at(const LayerPlan& layer, double frame, FrameRate fps) {
    if (!layer.animation || !layer.shape || !layer.shape->stroke) return std::nullopt;
    const AnimationTrackPlan* found=nullptr;
    for(const auto& track:layer.animation->tracks) if(track.property=="stroke_dash_offset") {
        if(found) throw std::runtime_error("layer has overlapping stroke_dash_offset tracks");
        found=&track;
    }
    if(!found) return std::nullopt;
    chronon3d::AnimatedValue<float> value{layer.shape->stroke->dash_offset};        for (const auto& key : found->keyframes)
            if (key.roving && (key.temporal_in_dx || key.temporal_in_dy ||
                key.temporal_out_dx || key.temporal_out_dy || key.bezier_curve))
                throw std::runtime_error("stroke dash-offset roving cannot use authored temporal handles");
    for(const auto& key:found->keyframes)
        add_animation_keyframe(value,*found,key,scalar_value(key,"stroke_dash_offset"),fps);
    value.compute_roving();
    value.compute_auto_beziers();
    const float result = value.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
    if(!std::isfinite(result)) throw std::runtime_error("animated stroke dash offset must be finite");
    return result;
}

std::optional<float> stroke_width_at(
    const LayerPlan& layer, double frame, float base, FrameRate fps) {
    if (!layer.animation || !layer.shape || !layer.shape->stroke) return std::nullopt;
    const AnimationTrackPlan* found = nullptr;
    for (const auto& track : layer.animation->tracks) {
        if (track.property != "stroke_width") continue;
        if (found) throw std::runtime_error("layer has duplicate stroke_width animation tracks");
        found = &track;
    }
    if (!found || found->keyframes.empty()) return std::nullopt;
    chronon3d::AnimatedValue<float> value{base};
    for (const auto& key : found->keyframes)
        add_animation_keyframe(value, *found, key, scalar_value(key, "stroke_width"), fps);
    value.compute_roving();
    value.compute_auto_beziers();
    const float result = value.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
    if (!std::isfinite(result) || result < 0.0f)
        throw std::runtime_error("animated stroke width must be finite and non-negative");
    return result;
}

std::optional<chronon3d::Color> stroke_color_at(
    const LayerPlan& layer, double frame, chronon3d::Color base, FrameRate fps) {
    if (!layer.animation || !layer.shape || !layer.shape->stroke) return std::nullopt;
    const AnimationTrackPlan* found = nullptr;
    for (const auto& track : layer.animation->tracks) {
        if (track.property != "stroke_color") continue;
        if (found) throw std::runtime_error("layer has duplicate stroke_color animation tracks");
        found = &track;
    }
    if (!found || found->keyframes.empty()) return std::nullopt;
    const auto color = [](const AnimationKeyframePlan& key) {
        if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                [](float component) {
                    return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                }))
            throw std::runtime_error("stroke_color keyframes require finite RGBA values in [0, 1]");
        return chronon3d::Color{key.value[0], key.value[1], key.value[2], key.value[3]};
    };
    chronon3d::AnimatedValue<chronon3d::Color> value{base};
    for (const auto& key : found->keyframes)
        add_animation_keyframe(value, *found, key, color(key), fps);
    value.compute_roving();
    value.compute_auto_beziers();
    return value.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
}

std::optional<float> shadow_scalar_at(
    const LayerPlan& layer, std::string_view property, double frame,
    float base, FrameRate fps) {
    if (!layer.animation) return std::nullopt;
    const AnimationTrackPlan* found = nullptr;
    for (const auto& track : layer.animation->tracks) {
        if (track.property != property) continue;
        if (found) throw std::runtime_error("layer has duplicate shadow animation tracks");
        found = &track;
    }
    if (!found || found->keyframes.empty()) return std::nullopt;
    chronon3d::AnimatedValue<float> value{base};
    for (const auto& key : found->keyframes)
        add_animation_keyframe(value, *found, key, scalar_value(key, property), fps);
    value.compute_roving();
    value.compute_auto_beziers();
    const float result = value.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
    if (!std::isfinite(result))
        throw std::runtime_error("animated shadow parameter must be finite");
    return result;
}

std::optional<chronon3d::Color> shadow_color_at(
    const LayerPlan& layer, std::string_view property, double frame,
    chronon3d::Color base, FrameRate fps) {
    if (!layer.animation) return std::nullopt;
    const AnimationTrackPlan* found = nullptr;
    for (const auto& track : layer.animation->tracks) {
        if (track.property != property) continue;
        if (found) throw std::runtime_error("layer has duplicate shadow color animation tracks");
        found = &track;
    }
    if (!found || found->keyframes.empty()) return std::nullopt;
    chronon3d::AnimatedValue<chronon3d::Color> value{base};
    for (const auto& key : found->keyframes) {
        if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                [](float component) {
                    return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                }))
            throw std::runtime_error("shadow color keyframes require finite RGBA values in [0, 1]");
        add_animation_keyframe(value, *found, key,
            chronon3d::Color{key.value[0], key.value[1], key.value[2], key.value[3]}, fps);
    }
    value.compute_roving();
    value.compute_auto_beziers();
    return value.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
}

chronon3d::TemporalCurve1D make_effect_time_curve(const AnimationTrackPlan& track) {
    chronon3d::AnimatedValue<float> curve{0.0f};
    for (const auto& key : track.keyframes)
        if (key.roving && (key.temporal_in_dx || key.temporal_in_dy ||
            key.temporal_out_dx || key.temporal_out_dy || key.bezier_curve))
            throw std::runtime_error("effect-time roving cannot use authored temporal handles");
    for(const auto& key:track.keyframes)
        add_animation_keyframe(curve,track,key,scalar_value(key,"effect_time"));
    curve.compute_roving();
    curve.compute_auto_beziers();
    return chronon3d::TemporalCurve1D{std::move(curve)};
}

chronon3d::EasingCurve animation_keyframe_easing(
    const AnimationTrackPlan& track, const AnimationKeyframePlan& key) {
    return keyframe_easing(track, key);
}

std::optional<chronon3d::Color> fill_color_at(
    const LayerPlan& layer, double frame, chronon3d::Color base, FrameRate fps) {
    if (!layer.animation) return std::nullopt;
    const AnimationTrackPlan* track = nullptr;
    for (const auto& candidate : layer.animation->tracks) {
        if (candidate.property != "fill_color") continue;
        if (track) throw std::runtime_error("layer has duplicate fill_color animation tracks");
        track = &candidate;
    }
    if (!track || track->keyframes.empty()) return std::nullopt;

    std::vector<const AnimationKeyframePlan*> keys;
    keys.reserve(track->keyframes.size());
    for (const auto& key : track->keyframes) keys.push_back(&key);
    std::stable_sort(keys.begin(), keys.end(), [](const auto* left, const auto* right) {
        return left->frame < right->frame;
    });
    const auto color = [](const AnimationKeyframePlan& key) {
        if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                [](float component) {
                    return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                }))
            throw std::runtime_error("fill_color keyframes require finite RGBA values in [0, 1]");
        return chronon3d::Color{key.value[0], key.value[1], key.value[2], key.value[3]};
    };
    for (std::size_t index = 1; index < keys.size(); ++index)
        if (keys[index - 1]->frame == keys[index]->frame)
            throw std::runtime_error("fill_color keyframe frames must be unique");
    const auto easing = track->easing == "spring"
        ? chronon3d::EasingCurve{chronon3d::Easing::Linear}
        : track_easing(track->easing, track->bezier_curve);
    chronon3d::AnimatedValue<chronon3d::Color> animated{base};
    for (const auto* key : keys)
        add_animation_keyframe(animated, *track, *key, color(*key), fps);
    animated.compute_roving();
    animated.compute_auto_beziers();
    const auto sample_key_segment = [&](std::size_t index, double sample_frame) {
        const double start = static_cast<double>(keys[index]->frame.integral());
        const double end = static_cast<double>(keys[index + 1]->frame.integral());
        if (sample_frame >= end || end <= start) return color(*keys[index + 1]);
        return animated.evaluate(sample_frame);
    };
    chronon3d::Color sampled = base;
    const double first_frame = static_cast<double>(keys.front()->frame.integral());
    if (frame < first_frame) {
        const float t = first_frame <= 0.0 ? 1.0f : static_cast<float>(
            std::clamp(frame / first_frame, 0.0, 1.0));
        sampled = easing.preset == chronon3d::Easing::Hold && !easing.cubic
            ? base
            : (keys.front()->interpolation || keys.front()->easing
                ? animated.evaluate(frame)
                : chronon3d::lerp(base, color(*keys.front()),
                                  std::clamp(easing.apply(t), 0.0f, 1.0f)));
    } else if (frame >= static_cast<double>(keys.back()->frame.integral())) {
        sampled = color(*keys.back());
    } else {
        for (std::size_t index = 1; index < keys.size(); ++index) {
            const double start = static_cast<double>(keys[index - 1]->frame.integral());
            const double end = static_cast<double>(keys[index]->frame.integral());
            if (frame >= end) continue;
            const float t = end <= start ? 1.0f : static_cast<float>(
                std::clamp((frame - start) / (end - start), 0.0, 1.0));
            sampled = easing.preset == chronon3d::Easing::Hold && !easing.cubic
                ? color(*keys[index - 1])
                : ((keys[index - 1]->interpolation || keys[index - 1]->easing ||
                    keys[index]->interpolation || keys[index]->easing)
                    ? sample_key_segment(index - 1, frame)
                    : chronon3d::lerp(
                        color(*keys[index - 1]), color(*keys[index]),
                        std::clamp(easing.apply(t), 0.0f, 1.0f)));
            break;
        }
    }
    if (!std::isfinite(sampled.r) || !std::isfinite(sampled.g) ||
        !std::isfinite(sampled.b) || !std::isfinite(sampled.a))
        throw std::runtime_error("animated fill_color must be finite");
    return sampled;
}

namespace {

graphics::GradientDefinition interpolate_authored_gradient(
    const graphics::GradientDefinition& a,
    const graphics::GradientDefinition& b,
    float amount) {
    graphics::GradientDefinition result = a;
    std::vector<float> color_positions;
    color_positions.reserve(a.color_stops.size() + b.color_stops.size());
    for (const auto& stop : a.color_stops) color_positions.push_back(stop.position);
    for (const auto& stop : b.color_stops) color_positions.push_back(stop.position);
    std::sort(color_positions.begin(), color_positions.end());
    color_positions.erase(std::unique(color_positions.begin(), color_positions.end()),
                          color_positions.end());
    result.color_stops.clear();
    result.color_stops.reserve(color_positions.size());
    for (const float position : color_positions) {
        const auto left = graphics::detail::sample_color_stops(a.color_stops, position);
        const auto right = graphics::detail::sample_color_stops(b.color_stops, position);
        result.color_stops.push_back({position,
            chronon3d::lerp(left, right, amount).to_srgb()});
    }

    if (!a.opacity_stops.empty() || !b.opacity_stops.empty()) {
        std::vector<float> opacity_positions{0.0f, 1.0f};
        for (const auto& stop : a.opacity_stops) opacity_positions.push_back(stop.position);
        for (const auto& stop : b.opacity_stops) opacity_positions.push_back(stop.position);
        std::sort(opacity_positions.begin(), opacity_positions.end());
        opacity_positions.erase(std::unique(opacity_positions.begin(), opacity_positions.end()),
                                opacity_positions.end());
        result.opacity_stops.clear();
        result.opacity_stops.reserve(opacity_positions.size());
        for (const float position : opacity_positions) {
            const float left = a.opacity_stops.empty()
                ? 1.0f : graphics::detail::sample_opacity_stops(a.opacity_stops, position);
            const float right = b.opacity_stops.empty()
                ? 1.0f : graphics::detail::sample_opacity_stops(b.opacity_stops, position);
            result.opacity_stops.push_back({position, left + (right - left) * amount});
        }
    }

    const auto mix = [amount](float x, float y) { return x + (y - x) * amount; };
    result.start = {mix(a.start.x, b.start.x), mix(a.start.y, b.start.y)};
    result.end = {mix(a.end.x, b.end.x), mix(a.end.y, b.end.y)};
    result.center = {mix(a.center.x, b.center.x), mix(a.center.y, b.center.y)};
    result.radius = mix(a.radius, b.radius);
    result.angle = mix(a.angle, b.angle);
    return result;
}

}  // namespace

std::optional<graphics::GradientDefinition> fill_gradient_at(
    const LayerPlan& layer, double frame) {
    if (!layer.shape || !layer.shape->gradient) return std::nullopt;
    const auto& base = *layer.shape->gradient;
    if (!layer.fill_gradient_animation || layer.fill_gradient_animation->keyframes.empty())
        return base;
    const auto& track = *layer.fill_gradient_animation;
    std::vector<const GradientKeyframePlan*> keys;
    keys.reserve(track.keyframes.size());
    for (const auto& key : track.keyframes) keys.push_back(&key);
    std::stable_sort(keys.begin(), keys.end(), [](const auto* left, const auto* right) {
        return left->frame < right->frame;
    });
    const auto easing = track_easing(track.easing, track.bezier_curve);
    const auto interpolate = [&](const graphics::GradientDefinition& a,
                                 const graphics::GradientDefinition& b,
                                 double start, double end) {
        const float t = static_cast<float>(std::clamp(
            (frame - start) / (end - start), 0.0, 1.0));
        const float eased = easing.apply(t);
        return interpolate_authored_gradient(a, b, std::clamp(eased, 0.0f, 1.0f));
    };
    const double first_frame = static_cast<double>(keys.front()->frame.integral());
    if (frame < first_frame) {
        if (first_frame <= 0.0) return keys.front()->value;
        if (easing.preset == chronon3d::Easing::Hold && !easing.cubic)
            return base;
        return interpolate(base, keys.front()->value, 0.0, first_frame);
    }
    for (std::size_t index = 1; index < keys.size(); ++index) {
        const double start = static_cast<double>(keys[index - 1]->frame.integral());
        const double end = static_cast<double>(keys[index]->frame.integral());
        if (frame < end) return interpolate(keys[index - 1]->value, keys[index]->value, start, end);
    }
    return keys.back()->value;
}

std::optional<chronon3d::camera_v1::CameraDescriptor>
make_camera_descriptor(const RenderPlan& plan) {
    if (!plan.camera) return std::nullopt;

    const auto& source_plan = *plan.camera;
    chronon3d::camera_v1::CameraDescriptor descriptor;
    descriptor.id = plan.job_id + "::render-plan-camera";
    descriptor.base.enabled = true;
    descriptor.base.position = {source_plan.position[0], source_plan.position[1], source_plan.position[2]};
    descriptor.base.rotation = {source_plan.rotation_deg[0], source_plan.rotation_deg[1], source_plan.rotation_deg[2]};
    descriptor.base.projection = chronon3d::camera_v1::FovProjection{
        chronon3d::AnimatedValue<float>{source_plan.fov_deg}};
    if (source_plan.type == "orthographic") {
        // Camera2_5D has no second projection implementation: use its existing
        // zoom optics surface rather than introducing a parallel camera model.
        descriptor.base.projection = chronon3d::camera_v1::ZoomProjection{
            chronon3d::AnimatedValue<float>{1000.0f * source_plan.zoom}};
    }

    if (!plan.camera_animation || plan.camera_animation->empty())
        return descriptor;

    chronon3d::camera_v1::PoseTracksSource tracks;
    tracks.position = chronon3d::AnimatedValue<chronon3d::Vec3>{descriptor.base.position};
    tracks.rotation = chronon3d::AnimatedValue<chronon3d::Vec3>{descriptor.base.rotation};
    tracks.fov_deg = chronon3d::AnimatedValue<float>{source_plan.fov_deg};
    tracks.zoom = chronon3d::AnimatedValue<float>{1000.0f * source_plan.zoom};
    for (const auto& track : *plan.camera_animation) {
        AnimationTrackPlan easing_track;
        easing_track.property = "camera";
        easing_track.easing = track.easing;
        easing_track.bezier_curve = track.bezier_curve;
        for (const auto& key : track.keyframes) {
            if (key.value.size() != 1)
                throw std::runtime_error("camera track keyframes require scalar values");
            const auto value = key.value.front();
            switch (track.property) {
                case CameraPropertyPlan::PositionX: {
                    auto current = descriptor.base.position;
                    current.x = value;
                    add_animation_keyframe(tracks.position, easing_track, key, current);
                    break;
                }
                case CameraPropertyPlan::PositionY: {
                    auto current = descriptor.base.position;
                    current.y = value;
                    add_animation_keyframe(tracks.position, easing_track, key, current);
                    break;
                }
                case CameraPropertyPlan::PositionZ: {
                    auto current = descriptor.base.position;
                    current.z = value;
                    add_animation_keyframe(tracks.position, easing_track, key, current);
                    break;
                }
                case CameraPropertyPlan::RotationX: {
                    auto current = descriptor.base.rotation;
                    current.x = value;
                    add_animation_keyframe(tracks.rotation, easing_track, key, current);
                    break;
                }
                case CameraPropertyPlan::RotationY: {
                    auto current = descriptor.base.rotation;
                    current.y = value;
                    add_animation_keyframe(tracks.rotation, easing_track, key, current);
                    break;
                }
                case CameraPropertyPlan::RotationZ: {
                    auto current = descriptor.base.rotation;
                    current.z = value;
                    add_animation_keyframe(tracks.rotation, easing_track, key, current);
                    break;
                }
                case CameraPropertyPlan::FovDeg:
                    add_animation_keyframe(tracks.fov_deg, easing_track, key, value);
                    break;
                case CameraPropertyPlan::Zoom:
                    add_animation_keyframe(tracks.zoom, easing_track, key, 1000.0f * value);
                    break;
            }
        }
    }
    tracks.position.compute_roving(); tracks.position.compute_auto_beziers();
    tracks.rotation.compute_roving(); tracks.rotation.compute_auto_beziers();
    tracks.fov_deg.compute_roving(); tracks.fov_deg.compute_auto_beziers();
    tracks.zoom.compute_roving(); tracks.zoom.compute_auto_beziers();
    descriptor.source = std::move(tracks);
    return descriptor;
}

chronon3d::DepthRole depth_role_value(std::string_view value) {
    if (value == "far_background") return chronon3d::DepthRole::FarBackground;
    if (value == "background") return chronon3d::DepthRole::Background;
    if (value == "midground") return chronon3d::DepthRole::Midground;
    if (value == "subject") return chronon3d::DepthRole::Subject;
    if (value == "atmosphere") return chronon3d::DepthRole::Atmosphere;
    if (value == "foreground") return chronon3d::DepthRole::Foreground;
    if (value == "overlay") return chronon3d::DepthRole::Overlay;
    throw std::runtime_error("unsupported depth role: " + std::string(value));
}

chronon3d::FitMode fit_mode(FitMode value) {
    switch (value) {
        case FitMode::Contain: return chronon3d::FitMode::Contain;
        case FitMode::Stretch: return chronon3d::FitMode::Stretch;
        case FitMode::None: return chronon3d::FitMode::None;
        case FitMode::Cover: return chronon3d::FitMode::Cover;
    }
    return chronon3d::FitMode::Cover;
}

void apply_text_animators(chronon3d::TextRunBuilder& builder, const LayerPlan& layer, FrameRate fps) {
    for (const auto& animator : layer.text_animators)
        builder.animator(compile_text_animator(animator, fps));
}

chronon3d::LayerTransitionSpec compile_transition(const TransitionPlan& plan, FrameRate fps) {
    chronon3d::LayerTransitionSpec spec;
    spec.transition_id = plan.kind;
    spec.duration = static_cast<double>(plan.duration_frames.integral()) * fps.den() / fps.num();
    spec.delay = static_cast<double>(plan.delay_frames.integral()) * fps.den() / fps.num();
    spec.easing = chronon3d::string_to_easing(plan.easing);
    spec.direction = plan.direction.empty()
        ? chronon3d::TransitionDirection::None
        : chronon3d::string_to_transition_direction(plan.direction);
    if (plan.kind == "crossfade") spec.parameters = chronon3d::CrossfadeParams{};
    else if (plan.kind == "slide") spec.parameters = chronon3d::SlideParams{plan.distance};
    else if (plan.kind == "wipe_linear") spec.parameters = chronon3d::WipeLinearParams{};
    else if (plan.kind == "smooth_wipe") spec.parameters = chronon3d::SmoothWipeParams{plan.feather};
    else if (plan.kind == "circle_iris")
        spec.parameters = chronon3d::CircleIrisParams{{plan.center[0], plan.center[1]}, plan.feather};
    else if (plan.kind == "flash")
        spec.parameters = chronon3d::FlashParams{{plan.color[0], plan.color[1], plan.color[2], plan.color[3]}};
    else if (plan.kind == "procedural_remotion")
        spec.parameters = chronon3d::ProceduralRemotionParams{
            plan.seed,
            {plan.inner_color[0], plan.inner_color[1], plan.inner_color[2], plan.inner_color[3]},
            {plan.mid_color[0], plan.mid_color[1], plan.mid_color[2], plan.mid_color[3]},
            {plan.outer_color[0], plan.outer_color[1], plan.outer_color[2], plan.outer_color[3]}};
    else if (plan.kind == "remotion")
        spec.parameters = chronon3d::RemotionParams{plan.speed, plan.remotion_direction, plan.angle};
    return spec;
}

void apply_layer_primitives(chronon3d::LayerBuilder& builder,
                            const LayerPlan& layer,
                            int canvas_width,
                            int canvas_height,
                            FrameRate fps,
                            const FollowPathEvaluators* precompiled_follow_path) {
    if (layer.transition_in) builder.transition_in(compile_transition(*layer.transition_in, fps));
    if (layer.transition_out) builder.transition_out(compile_transition(*layer.transition_out, fps));
    if (layer.parent) builder.parent(*layer.parent);
    if (layer.track_matte) {
        switch (layer.track_matte->type) {
            case TrackMatteTypePlan::Alpha:
                builder.track_matte_alpha(layer.track_matte->source); break;
            case TrackMatteTypePlan::AlphaInverted:
                builder.track_matte_alpha_inverted(layer.track_matte->source); break;
            case TrackMatteTypePlan::Luma:
                builder.track_matte_luma(layer.track_matte->source); break;
            case TrackMatteTypePlan::LumaInverted:
                builder.track_matte_luma_inverted(layer.track_matte->source); break;
        }
    }
    if (layer.time_remap) {
        if (layer.time_remap->speed)
            builder.speed(*layer.time_remap->speed);
        if (layer.time_remap->reverse)
            builder.reverse();
        if (layer.time_remap->freeze_frame)
            builder.freeze_frame(*layer.time_remap->freeze_frame);
        if (layer.time_remap->animation) {
            auto& remap = builder.time_remap_anim();
            for (const auto& key : layer.time_remap->animation->keyframes)
                add_animation_keyframe(remap, *layer.time_remap->animation, key,
                                       scalar_value(key, "time_remap"));
            remap.compute_roving();
            remap.compute_auto_beziers();
        }
    }
    if (layer.enable_3d) builder.enable_3d();
    if (layer.depth_role) {
        builder.depth_role(depth_role_value(*layer.depth_role));
        builder.depth_offset(layer.depth_offset);
    }
    if (layer.start_frame) builder.from(*layer.start_frame);
    if (layer.duration_frames) builder.duration(*layer.duration_frames);
    if (layer.position_dimensions >= 2) {
        // Render-plan geometry (Text, Shape) is authored in canvas coordinates
        // with a top-left box origin, while LayerBuilder transforms are centred
        // on the canvas. Convert the box origin once; motion tracks remain
        // additive offsets from this resolved placement.
        if (layer.type == LayerType::Text || layer.type == LayerType::Shape) {
            builder.position({
                layer.position[0] - static_cast<float>(canvas_width) * 0.5f,
                layer.position[1] - static_cast<float>(canvas_height) * 0.5f,
                layer.position[2]});
        } else {
            builder.position({layer.position[0], layer.position[1], layer.position[2]});
        }
    }
    if (layer.scale_dimensions >= 2)
        builder.scale({layer.scale[0], layer.scale[1], layer.scale[2]});
    if (layer.rotation_dimensions > 0)
        builder.rotate({layer.rotation[0], layer.rotation[1], layer.rotation[2]});
    builder.skew({layer.skew_angle_deg, layer.skew_axis_deg});
    if (layer.blend_mode) builder.blend(*layer.blend_mode);
    if (layer.opacity) builder.opacity(*layer.opacity);
    apply_animation_tracks(builder, layer, canvas_width, canvas_height, fps,
                           precompiled_follow_path);
}

float evaluate_scalar_track(const AnimationTrackPlan& track, double frame,
                            float base, FrameRate fps) {
    chronon3d::AnimatedValue<float> animated{base};
    for (const auto& key : track.keyframes)
        add_animation_keyframe(animated, track, key, scalar_value(key, track.property), fps);
    animated.compute_roving();
    animated.compute_auto_beziers();
    return animated.evaluate(chronon3d::SampleTime::from_frame(frame, fps));
}


std::optional<chronon3d::DirectionalBlurParams> motion_blur_params_at(
    const LayerPlan& layer, double frame, FrameRate fps,
    const FollowPathEvaluators* precompiled_follow_path) {
    if (!layer.motion_blur || (!layer.animation &&
        !(precompiled_follow_path && precompiled_follow_path->position))) return std::nullopt;
    const double half_exposure_frames =
        static_cast<double>(layer.motion_blur->shutter_angle) / 720.0;
    chronon3d::Vec3 delta{};
    if (precompiled_follow_path && precompiled_follow_path->position) {
        delta = precompiled_follow_path->position(frame + half_exposure_frames) -
                precompiled_follow_path->position(frame - half_exposure_frames);
    } else {
    const chronon3d::Vec3 base{layer.position[0], layer.position[1], layer.position[2]};
    chronon3d::AnimatedValue<chronon3d::Vec3> position{base};
    const auto data = position_animation_data(layer, base);
    if (data.keyframes.empty()) return std::nullopt;
    for (const auto& key : data.keyframes) {
        if (key.timing.interpolation == "bezier" && !key.timing.bezier_curve) {
            position.add_vector_temporal_keyframe(key.frame, key.value,
                chronon3d::InterpMode::Bezier,
                key.timing.temporal_in_dx.value_or(0.0f), key.temporal_in_value,
                key.timing.temporal_out_dx.value_or(0.0f), key.temporal_out_value,
                key.timing.roving, chronon3d::EasingCurve{chronon3d::Easing::Linear});
        } else {
            add_animation_keyframe(position, data.timing, key.timing, key.value, fps);
        }
    }
    position.compute_roving();
    position.compute_auto_beziers();
    delta = position.evaluate(frame + half_exposure_frames) -
            position.evaluate(frame - half_exposure_frames);
    }
    const float length = glm::length(glm::vec2{delta.x, delta.y});
    if (!std::isfinite(length) || length <= 1.0e-4f) return std::nullopt;
    chronon3d::DirectionalBlurParams result;
    result.angle = glm::degrees(std::atan2(delta.y, delta.x));
    result.length = length;
    result.samples = static_cast<int>(layer.motion_blur->samples);
    return result;
}

}  // namespace chronon3d::render_plan::detail
