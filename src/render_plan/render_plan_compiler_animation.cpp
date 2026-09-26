#include "render_plan_compiler_detail.hpp"

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>
#include <cmath>
#include <string>
#include <string_view>
#include <type_traits>

namespace chronon3d::render_plan::detail {
namespace {

chronon3d::EasingCurve track_easing(
    std::string_view value,
    const std::optional<std::array<float, 4>>& bezier_curve = std::nullopt) {
    using chronon3d::Easing;
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
    if (value == "linear") return {Easing::Linear};
    if (value == "in_quad") return {Easing::InQuad};
    if (value == "out_quad") return {Easing::OutQuad};
    if (value == "in_out_quad") return {Easing::InOutQuad};
    if (value == "in_cubic") return {Easing::InCubic};
    if (value == "out_cubic") return {Easing::OutCubic};
    if (value == "in_out_cubic") return {Easing::InOutCubic};
    if (value == "in_expo") return {Easing::InExpo};
    if (value == "out_expo") return {Easing::OutExpo};
    if (value == "in_out_expo") return {Easing::InOutExpo};
    if (value == "in_sine") return {Easing::InSine};
    if (value == "out_sine") return {Easing::OutSine};
    if (value == "in_out_sine") return {Easing::InOutSine};
    if (value == "in_back") return {Easing::InBack};
    if (value == "out_back") return {Easing::OutBack};
    if (value == "in_out_back") return {Easing::InOutBack};
    if (value == "in_elastic") return {Easing::InElastic};
    if (value == "out_elastic") return {Easing::OutElastic};
    if (value == "in_out_elastic") return {Easing::InOutElastic};
    if (value == "in_bounce") return {Easing::InBounce};
    if (value == "out_bounce") return {Easing::OutBounce};
    if (value == "in_out_bounce") return {Easing::InOutBounce};
    if (value == "smoothstep") return {Easing::Smoothstep};
    if (value == "hold") return {Easing::Hold};
    throw std::runtime_error("unsupported animation easing: " + std::string(value));
}

chronon3d::EasingCurve keyframe_easing(const AnimationTrackPlan& track,
                                       const AnimationKeyframePlan& key) {
    if (key.interpolation) {
        if (key.easing || key.bezier_curve)
            throw std::runtime_error("keyframe interpolation cannot be combined with easing or bezier_curve");
        if (*key.interpolation == "linear") return {chronon3d::Easing::Linear};
        if (*key.interpolation == "hold") return {chronon3d::Easing::Hold};
        if (*key.interpolation == "bezier") {
            if (!key.bezier_curve)
                throw std::runtime_error("keyframe interpolation='bezier' requires bezier_curve");
            return track_easing("bezier", key.bezier_curve);
        }
        throw std::runtime_error("unsupported keyframe interpolation: " + *key.interpolation);
    }
    if (key.easing || key.bezier_curve) {
        if (key.bezier_curve && !key.easing)
            throw std::runtime_error("per-key bezier_curve requires easing='bezier'");
        return track_easing(key.easing.value_or(track.easing),
                            key.bezier_curve ? key.bezier_curve : track.bezier_curve);
    }
    return track_easing(track.easing, track.bezier_curve);
}

float scalar_value(const AnimationKeyframePlan& key, std::string_view property) {
    if (key.value.size() != 1)
        throw std::runtime_error("animation property '" + std::string(property) +
                                 "' requires scalar keyframe values");
    return key.value.front();
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
        chronon3d::EasingCurve easing;
    };
    std::vector<Keyframe> keyframes;
};

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

    const AnimationTrackPlan* reference = vector_track;
    if (!reference) {
        for (const auto* component : component_tracks)
            if (component) { reference = component; break; }
        for (const auto* component : component_tracks) {
            if (!component || component == reference) continue;
            if (component->easing != reference->easing ||
                component->bezier_curve != reference->bezier_curve ||
                component->keyframes.size() != reference->keyframes.size())
                throw std::runtime_error(
                    "position component tracks must share keyframes and easing");
            for (std::size_t index = 0; index < reference->keyframes.size(); ++index)
                if (component->keyframes[index].frame != reference->keyframes[index].frame ||
                    component->keyframes[index].interpolation != reference->keyframes[index].interpolation ||
                    component->keyframes[index].easing != reference->keyframes[index].easing ||
                    component->keyframes[index].bezier_curve != reference->keyframes[index].bezier_curve)
                    throw std::runtime_error(
                        "position component tracks must share keyframes and easing");
        }
    }
    const auto& reference_keys = reference->keyframes;
    result.keyframes.reserve(reference_keys.size());
    for (std::size_t index = 0; index < reference_keys.size(); ++index) {
        auto value = base;
        if (vector_track) {
            value = vector_value(reference_keys[index], "position", base);
        } else {
            for (std::size_t component = 0; component < component_tracks.size(); ++component) {
                const auto* track = component_tracks[component];
                if (!track) continue;
                const float offset = scalar_value(track->keyframes[index], track->property);
                if (component == 0) value.x += offset;
                else if (component == 1) value.y += offset;
                else value.z += offset;
            }
        }
        result.keyframes.push_back({reference_keys[index].frame, value,
                                    keyframe_easing(*reference, reference_keys[index])});
    }
    return result;
}


chronon3d::TextSelectorUnit text_selector_unit(std::string_view value) {
    using chronon3d::TextSelectorUnit;
    if (value == "glyph") return TextSelectorUnit::Glyph;
    if (value == "grapheme") return TextSelectorUnit::Grapheme;
    if (value == "character") return TextSelectorUnit::Character;
    if (value == "word") return TextSelectorUnit::Word;
    if (value == "line") return TextSelectorUnit::Line;
    throw std::runtime_error("unsupported text selector unit: " + std::string(value));
}

chronon3d::TextSelectorShape text_selector_shape(std::string_view value) {
    using chronon3d::TextSelectorShape;
    if (value == "square") return TextSelectorShape::Square;
    if (value == "ramp_up") return TextSelectorShape::RampUp;
    if (value == "ramp_down") return TextSelectorShape::RampDown;
    if (value == "triangle") return TextSelectorShape::Triangle;
    if (value == "round") return TextSelectorShape::Round;
    if (value == "smooth") return TextSelectorShape::Smooth;
    if (value == "wiggly") return TextSelectorShape::Wiggly;
    throw std::runtime_error("unsupported text selector shape: " + std::string(value));
}

chronon3d::TextSelectorOrder text_selector_order(std::string_view value) {
    using chronon3d::TextSelectorOrder;
    if (value == "forward") return TextSelectorOrder::Forward;
    if (value == "reverse") return TextSelectorOrder::Reverse;
    if (value == "from_center") return TextSelectorOrder::FromCenter;
    if (value == "to_center") return TextSelectorOrder::ToCenter;
    if (value == "random") return TextSelectorOrder::Random;
    throw std::runtime_error("unsupported text selector order: " + std::string(value));
}

chronon3d::SelectorCombineMode text_selector_combine(std::string_view value) {
    using chronon3d::SelectorCombineMode;
    if (value == "replace") return SelectorCombineMode::Replace;
    if (value == "add") return SelectorCombineMode::Add;
    if (value == "subtract") return SelectorCombineMode::Subtract;
    if (value == "intersect") return SelectorCombineMode::Intersect;
    if (value == "min") return SelectorCombineMode::Min;
    if (value == "max") return SelectorCombineMode::Max;
    throw std::runtime_error("unsupported text selector combine mode: " + std::string(value));
}

template <typename T>
chronon3d::AnimatedValue<T> animated_text_value(
    const AnimationTrackPlan& track, T fallback, std::string_view property) {
    chronon3d::AnimatedValue<T> result{fallback};
    for (const auto& key : track.keyframes) {
        const auto easing = keyframe_easing(track, key);
        if constexpr (std::is_same_v<T, float>) {
            result.add_keyframe(key.frame, scalar_value(key, property), easing);
        } else {
            result.add_keyframe(key.frame, vector_value(key, property, fallback), easing);
        }
    }
    return result;
}

// `rotation` keyframes are scalar (Z rotation in degrees) or full XYZ triples.
chronon3d::AnimatedValue<chronon3d::Vec3> animated_text_rotation(
    const AnimationTrackPlan& track) {
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
        result.add_keyframe(key.frame, value, keyframe_easing(track, key));
    }
    return result;
}

// `fill_color` keyframes are always RGBA quadruples in 0..1.
chronon3d::AnimatedValue<chronon3d::Color> animated_text_color(
    const AnimationTrackPlan& track, std::string_view property) {
    chronon3d::AnimatedValue<chronon3d::Color> result{
        chronon3d::Color{1.0f, 1.0f, 1.0f, 1.0f}};
    for (const auto& key : track.keyframes) {
        if (key.value.size() != 4)
            throw std::runtime_error("animation property '" + std::string(property) +
                                     "' requires 4 keyframe components");
        result.add_keyframe(
            key.frame,
            chronon3d::Color{key.value[0], key.value[1], key.value[2], key.value[3]},
            keyframe_easing(track, key));
    }
    return result;
}

chronon3d::AnimatedValue<chronon3d::Vec3> animated_text_vec3_component(
    const AnimationTrackPlan& track, chronon3d::Vec3 fallback,
    std::string_view property) {
    chronon3d::AnimatedValue<chronon3d::Vec3> result{fallback};
    for (const auto& key : track.keyframes) {
        auto value = fallback;
        if (property == "position_x" || property == "scale_x")
            value.x = scalar_value(key, property);
        else if (property == "position_y" || property == "scale_y")
            value.y = scalar_value(key, property);
        else
            value.z = scalar_value(key, property);
        result.add_keyframe(key.frame, value, keyframe_easing(track, key));
    }
    return result;
}

chronon3d::GlyphSelectorSpec compile_text_selector(const TextSelectorPlan& plan) {
    chronon3d::GlyphSelectorSpec selector;
    selector.id = plan.id;
    selector.semantic_id = plan.semantic_id;
    selector.unit = text_selector_unit(plan.unit);
    selector.shape = text_selector_shape(plan.shape);
    selector.order = text_selector_order(plan.order);
    selector.combine = text_selector_combine(plan.combine);
    selector.start = animated_text_value(plan.start, 0.0f, "selector.start");
    selector.end = animated_text_value(plan.end, 100.0f, "selector.end");
    selector.offset = animated_text_value(plan.offset, 0.0f, "selector.offset");
    selector.amount = animated_text_value(plan.amount, 100.0f, "selector.amount");
    selector.exclude_spaces = plan.exclude_spaces;
    selector.randomize_order = plan.randomize_order;
    selector.random_seed = plan.random_seed;
    selector.wiggly = chronon3d::WigglySelectorParams{
        plan.wiggly_frequency, plan.wiggly_amplitude, plan.wiggly_seed};
    return selector;
}

chronon3d::TextAnimatorSpec compile_text_animator(const TextAnimatorPlan& plan) {
    chronon3d::TextAnimatorSpec animator;
    animator.id = plan.id;
    for (const auto& selector : plan.selectors)
        animator.selectors.push_back(compile_text_selector(selector));
    for (const auto& track : plan.properties) {
        if (track.property == "position" || track.property == "position_x" ||
            track.property == "position_y") {
            animator.properties.push_back(chronon3d::PositionProperty{
                track.property == "position"
                    ? animated_text_value(track, chronon3d::Vec3{}, track.property)
                    : animated_text_vec3_component(track, chronon3d::Vec3{}, track.property)});
        } else if (track.property == "scale" || track.property == "scale_x" ||
                   track.property == "scale_y") {
            animator.properties.push_back(chronon3d::ScaleProperty{
                track.property == "scale"
                    ? animated_text_value(track, chronon3d::Vec3{1.0f, 1.0f, 1.0f}, track.property)
                    : animated_text_vec3_component(
                          track, chronon3d::Vec3{1.0f, 1.0f, 1.0f}, track.property)});
        } else if (track.property == "opacity") {
            animator.properties.push_back(chronon3d::OpacityProperty{
                animated_text_value(track, 1.0f, track.property)});
        } else if (track.property == "blur") {
            animator.properties.push_back(chronon3d::BlurProperty{
                animated_text_value(track, 0.0f, track.property)});
        } else if (track.property == "tracking") {
            animator.properties.push_back(chronon3d::TrackingProperty{
                animated_text_value(track, 0.0f, track.property)});
        } else if (track.property == "rotation") {
            animator.properties.push_back(chronon3d::RotationProperty{
                animated_text_rotation(track)});
        } else if (track.property == "fill_color") {
            animator.properties.push_back(chronon3d::FillColorProperty{
                animated_text_color(track, track.property)});
        } else {
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
                            int canvas_height) {
    if (!layer.animation) return;
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

    AxisTrackGroup pos_tracks;
    AxisTrackGroup scale_tracks;
    AxisTrackGroup rot_tracks;
    const AnimationTrackPlan* opacity_track = nullptr;
    const AnimationTrackPlan* blur_track = nullptr;

    for (const auto& track : layer.animation->tracks) {
        if (track.property == "position") {
            if (pos_tracks.full) throw std::runtime_error("layer '" + layer.id + "' has duplicate position tracks");
            pos_tracks.full = &track;
        } else if (track.property == "position_x") {
            if (pos_tracks.x) throw std::runtime_error("layer '" + layer.id + "' has duplicate position_x tracks");
            pos_tracks.x = &track;
        } else if (track.property == "position_y") {
            if (pos_tracks.y) throw std::runtime_error("layer '" + layer.id + "' has duplicate position_y tracks");
            pos_tracks.y = &track;
        } else if (track.property == "position_z") {
            if (pos_tracks.z) throw std::runtime_error("layer '" + layer.id + "' has duplicate position_z tracks");
            pos_tracks.z = &track;
        } else if (track.property == "scale") {
            if (scale_tracks.full) throw std::runtime_error("layer '" + layer.id + "' has duplicate scale tracks");
            scale_tracks.full = &track;
        } else if (track.property == "scale_x") {
            if (scale_tracks.x) throw std::runtime_error("layer '" + layer.id + "' has duplicate scale_x tracks");
            scale_tracks.x = &track;
        } else if (track.property == "scale_y") {
            if (scale_tracks.y) throw std::runtime_error("layer '" + layer.id + "' has duplicate scale_y tracks");
            scale_tracks.y = &track;
        } else if (track.property == "scale_z") {
            if (scale_tracks.z) throw std::runtime_error("layer '" + layer.id + "' has duplicate scale_z tracks");
            scale_tracks.z = &track;
        } else if (track.property == "rotation") {
            if (rot_tracks.full) throw std::runtime_error("layer '" + layer.id + "' has duplicate rotation tracks");
            rot_tracks.full = &track;
        } else if (track.property == "rotation_x") {
            if (rot_tracks.x) throw std::runtime_error("layer '" + layer.id + "' has duplicate rotation_x tracks");
            rot_tracks.x = &track;
        } else if (track.property == "rotation_y") {
            if (rot_tracks.y) throw std::runtime_error("layer '" + layer.id + "' has duplicate rotation_y tracks");
            rot_tracks.y = &track;
        } else if (track.property == "rotation_z") {
            if (rot_tracks.z) throw std::runtime_error("layer '" + layer.id + "' has duplicate rotation_z tracks");
            rot_tracks.z = &track;
        } else if (track.property == "opacity") {
            if (opacity_track) throw std::runtime_error("layer '" + layer.id + "' has duplicate opacity tracks");
            opacity_track = &track;
        } else if (track.property == "blur") {
            if (blur_track) throw std::runtime_error("layer '" + layer.id + "' has duplicate blur tracks");
            blur_track = &track;
        } else {
            throw std::runtime_error("unsupported primitive animation property: " + track.property);
        }
    }

    if (pos_tracks.full && (pos_tracks.x || pos_tracks.y || pos_tracks.z))
        throw std::runtime_error("layer '" + layer.id + "' mixes 'position' with component tracks");
    if (scale_tracks.full && (scale_tracks.x || scale_tracks.y || scale_tracks.z))
        throw std::runtime_error("layer '" + layer.id + "' mixes 'scale' with component tracks");
    if (rot_tracks.full && (rot_tracks.x || rot_tracks.y || rot_tracks.z))
        throw std::runtime_error("layer '" + layer.id + "' mixes 'rotation' with component tracks");

    auto make_scalar_eval = [](const AnimationTrackPlan* track, float fallback) -> std::optional<chronon3d::AnimatedValue<float>> {
        if (!track) return std::nullopt;
        chronon3d::AnimatedValue<float> anim(fallback);
        for (const auto& key : track->keyframes) {
            anim.add_keyframe(key.frame, scalar_value(key, track->property), keyframe_easing(*track, key));
        }
        return anim;
    };

    auto dominant_easing = [](const AnimationTrackPlan* x,
                              const AnimationTrackPlan* y,
                              const AnimationTrackPlan* z,
                              Frame f) -> chronon3d::EasingCurve {
        chronon3d::EasingCurve result{chronon3d::Easing::Linear};
        for (const auto* t : {x, y, z}) {
            if (t) {
                for (const auto& k : t->keyframes) {
                    if (k.frame == f) {
                        const auto curve = keyframe_easing(*t, k);
                        if (curve != chronon3d::Easing::Linear || result == chronon3d::Easing::Linear) {
                            result = curve;
                        }
                    }
                }
            }
        }
        return result;
    };

    if (pos_tracks.full) {
        auto& animated = builder.position_anim();
        animated.set(base_position);
        for (const auto& key : pos_tracks.full->keyframes) {
            animated.add_keyframe(key.frame, vector_value(key, pos_tracks.full->property, base_position), keyframe_easing(*pos_tracks.full, key));
        }
    } else if (pos_tracks.x || pos_tracks.y || pos_tracks.z) {
        auto& animated = builder.position_anim();
        animated.set(base_position);
        auto eval_x = make_scalar_eval(pos_tracks.x, 0.0f);
        auto eval_y = make_scalar_eval(pos_tracks.y, 0.0f);
        auto eval_z = make_scalar_eval(pos_tracks.z, 0.0f);

        std::set<Frame> frames;
        for (const auto* t : {pos_tracks.x, pos_tracks.y, pos_tracks.z}) {
            if (t) {
                for (const auto& k : t->keyframes) frames.insert(k.frame);
            }
        }

        for (const auto& f : frames) {
            chronon3d::Vec3 v = base_position;
            if (eval_x) v.x += eval_x->evaluate(static_cast<double>(f.integral()));
            if (eval_y) v.y += eval_y->evaluate(static_cast<double>(f.integral()));
            if (eval_z) v.z += eval_z->evaluate(static_cast<double>(f.integral()));
            animated.add_keyframe(f, v, dominant_easing(pos_tracks.x, pos_tracks.y, pos_tracks.z, f));
        }
    }

    if (scale_tracks.full) {
        auto& animated = builder.scale_anim();
        animated.set(base_scale);
        for (const auto& key : scale_tracks.full->keyframes) {
            animated.add_keyframe(key.frame, vector_value(key, scale_tracks.full->property, base_scale), keyframe_easing(*scale_tracks.full, key));
        }
    } else if (scale_tracks.x || scale_tracks.y || scale_tracks.z) {
        auto& animated = builder.scale_anim();
        animated.set(base_scale);
        auto eval_x = make_scalar_eval(scale_tracks.x, base_scale.x);
        auto eval_y = make_scalar_eval(scale_tracks.y, base_scale.y);
        auto eval_z = make_scalar_eval(scale_tracks.z, base_scale.z);

        std::set<Frame> frames;
        for (const auto* t : {scale_tracks.x, scale_tracks.y, scale_tracks.z}) {
            if (t) {
                for (const auto& k : t->keyframes) frames.insert(k.frame);
            }
        }

        for (const auto& f : frames) {
            chronon3d::Vec3 v = base_scale;
            if (eval_x) v.x = eval_x->evaluate(static_cast<double>(f.integral()));
            if (eval_y) v.y = eval_y->evaluate(static_cast<double>(f.integral()));
            if (eval_z) v.z = eval_z->evaluate(static_cast<double>(f.integral()));
            animated.add_keyframe(f, v, dominant_easing(scale_tracks.x, scale_tracks.y, scale_tracks.z, f));
        }
    }

    if (rot_tracks.full) {
        auto& animated = builder.rotate_anim();
        animated.set(base_rotation);
        for (const auto& key : rot_tracks.full->keyframes) {
            animated.add_keyframe(key.frame, vector_value(key, rot_tracks.full->property, base_rotation), keyframe_easing(*rot_tracks.full, key));
        }
    } else if (rot_tracks.x || rot_tracks.y || rot_tracks.z) {
        auto& animated = builder.rotate_anim();
        animated.set(base_rotation);
        auto eval_x = make_scalar_eval(rot_tracks.x, base_rotation.x);
        auto eval_y = make_scalar_eval(rot_tracks.y, base_rotation.y);
        auto eval_z = make_scalar_eval(rot_tracks.z, base_rotation.z);

        std::set<Frame> frames;
        for (const auto* t : {rot_tracks.x, rot_tracks.y, rot_tracks.z}) {
            if (t) {
                for (const auto& k : t->keyframes) frames.insert(k.frame);
            }
        }

        for (const auto& f : frames) {
            chronon3d::Vec3 v = base_rotation;
            if (eval_x) v.x = eval_x->evaluate(static_cast<double>(f.integral()));
            if (eval_y) v.y = eval_y->evaluate(static_cast<double>(f.integral()));
            if (eval_z) v.z = eval_z->evaluate(static_cast<double>(f.integral()));
            animated.add_keyframe(f, v, dominant_easing(rot_tracks.x, rot_tracks.y, rot_tracks.z, f));
        }
    }

    if (opacity_track) {
        auto& animated = builder.opacity_anim();
        animated.set(layer.opacity.value_or(1.0f));
        for (const auto& key : opacity_track->keyframes)
            animated.add_keyframe(key.frame, scalar_value(key, opacity_track->property), keyframe_easing(*opacity_track, key));
    }
    if (blur_track) {
        auto& animated = builder.blur_anim();
        animated.set(0.0f);
        for (const auto& key : blur_track->keyframes)
            animated.add_keyframe(key.frame, scalar_value(key, blur_track->property), keyframe_easing(*blur_track, key));
    }
}

}  // namespace

std::optional<float> stroke_dash_offset_at(const LayerPlan& layer, double frame) {
    if (!layer.animation || !layer.shape || !layer.shape->stroke) return std::nullopt;
    const AnimationTrackPlan* found=nullptr;
    for(const auto& track:layer.animation->tracks) if(track.property=="stroke_dash_offset") {
        if(found) throw std::runtime_error("layer has overlapping stroke_dash_offset tracks");
        found=&track;
    }
    if(!found) return std::nullopt;
    chronon3d::AnimatedValue<float> value{layer.shape->stroke->dash_offset};
    for(const auto& key:found->keyframes) value.add_keyframe(key.frame,scalar_value(key,"stroke_dash_offset"),keyframe_easing(*found,key));
    const float result=value.evaluate(frame);
    if(!std::isfinite(result)) throw std::runtime_error("animated stroke dash offset must be finite");
    return result;
}

chronon3d::TemporalCurve1D make_effect_time_curve(const AnimationTrackPlan& track) {
    chronon3d::AnimatedValue<float> curve{0.0f};
    for(const auto& key:track.keyframes)
        curve.add_keyframe(key.frame,scalar_value(key,"effect_time"),keyframe_easing(track,key));
    return chronon3d::TemporalCurve1D{std::move(curve)};
}

chronon3d::EasingCurve animation_keyframe_easing(
    const AnimationTrackPlan& track, const AnimationKeyframePlan& key) {
    return keyframe_easing(track, key);
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
        easing_track.easing = track.easing;
        easing_track.bezier_curve = track.bezier_curve;
        for (const auto& key : track.keyframes) {
            if (key.value.size() != 1)
                throw std::runtime_error("camera track keyframes require scalar values");
            const auto value = key.value.front();
            const auto easing = keyframe_easing(easing_track, key);
            switch (track.property) {
                case CameraPropertyPlan::PositionX: {
                    auto current = descriptor.base.position;
                    current.x = value;
                    tracks.position.add_keyframe(key.frame, current, easing);
                    break;
                }
                case CameraPropertyPlan::PositionY: {
                    auto current = descriptor.base.position;
                    current.y = value;
                    tracks.position.add_keyframe(key.frame, current, easing);
                    break;
                }
                case CameraPropertyPlan::PositionZ: {
                    auto current = descriptor.base.position;
                    current.z = value;
                    tracks.position.add_keyframe(key.frame, current, easing);
                    break;
                }
                case CameraPropertyPlan::RotationX: {
                    auto current = descriptor.base.rotation;
                    current.x = value;
                    tracks.rotation.add_keyframe(key.frame, current, easing);
                    break;
                }
                case CameraPropertyPlan::RotationY: {
                    auto current = descriptor.base.rotation;
                    current.y = value;
                    tracks.rotation.add_keyframe(key.frame, current, easing);
                    break;
                }
                case CameraPropertyPlan::RotationZ: {
                    auto current = descriptor.base.rotation;
                    current.z = value;
                    tracks.rotation.add_keyframe(key.frame, current, easing);
                    break;
                }
                case CameraPropertyPlan::FovDeg:
                    tracks.fov_deg.add_keyframe(key.frame, value, easing);
                    break;
                case CameraPropertyPlan::Zoom:
                    tracks.zoom.add_keyframe(key.frame, 1000.0f * value, easing);
                    break;
            }
        }
    }
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

void apply_text_animators(chronon3d::TextRunBuilder& builder, const LayerPlan& layer) {
    for (const auto& animator : layer.text_animators)
        builder.animator(compile_text_animator(animator));
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
                            FrameRate fps) {
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
                remap.add_keyframe(key.frame, scalar_value(key, "time_remap"),
                                   keyframe_easing(*layer.time_remap->animation, key));
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
    if (layer.blend_mode) builder.blend(*layer.blend_mode);
    if (layer.opacity) builder.opacity(*layer.opacity);
    apply_animation_tracks(builder, layer, canvas_width, canvas_height);
}


std::optional<chronon3d::DirectionalBlurParams> motion_blur_params_at(
    const LayerPlan& layer, double frame) {
    if (!layer.motion_blur || !layer.animation) return std::nullopt;
    const chronon3d::Vec3 base{layer.position[0], layer.position[1], layer.position[2]};
    chronon3d::AnimatedValue<chronon3d::Vec3> position{base};
    const auto data = position_animation_data(layer, base);
    if (data.keyframes.empty()) return std::nullopt;
    for (const auto& key : data.keyframes)
        position.add_keyframe(key.frame, key.value, key.easing);
    const double half_exposure_frames =
        static_cast<double>(layer.motion_blur->shutter_angle) / 720.0;
    const auto delta = position.evaluate(frame + half_exposure_frames) -
                       position.evaluate(frame - half_exposure_frames);
    const float length = glm::length(glm::vec2{delta.x, delta.y});
    if (!std::isfinite(length) || length <= 1.0e-4f) return std::nullopt;
    chronon3d::DirectionalBlurParams result;
    result.angle = glm::degrees(std::atan2(delta.y, delta.x));
    result.length = length;
    result.samples = static_cast<int>(layer.motion_blur->samples);
    return result;
}

}  // namespace chronon3d::render_plan::detail
