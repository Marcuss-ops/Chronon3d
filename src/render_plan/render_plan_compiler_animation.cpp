#include "render_plan_compiler_detail.hpp"

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace chronon3d::render_plan::detail {
namespace {

chronon3d::EasingCurve track_easing(std::string_view value) {
    using chronon3d::Easing;
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
        const auto easing = track_easing(track.easing);
        if constexpr (std::is_same_v<T, float>) {
            result.add_keyframe(key.frame, scalar_value(key, property), easing);
        } else {
            result.add_keyframe(key.frame, vector_value(key, property, fallback), easing);
        }
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
        result.add_keyframe(key.frame, value, track_easing(track.easing));
    }
    return result;
}

chronon3d::GlyphSelectorSpec compile_text_selector(const TextSelectorPlan& plan) {
    chronon3d::GlyphSelectorSpec selector;
    selector.id = plan.id;
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
        } else {
            throw std::runtime_error("unsupported text animator property: " + track.property);
        }
    }
    if (animator.selectors.empty() || animator.properties.empty())
        throw std::runtime_error("text animator '" + animator.id + "' requires selectors and properties");
    return animator;
}

void apply_animation_tracks(chronon3d::LayerBuilder& builder, const LayerPlan& layer) {
    if (!layer.animation) return;
    const chronon3d::Vec3 base_position{layer.position[0], layer.position[1], layer.position[2]};
    const chronon3d::Vec3 base_scale{layer.scale[0], layer.scale[1], layer.scale[2]};
    const chronon3d::Vec3 base_rotation{layer.rotation[0], layer.rotation[1], layer.rotation[2]};

    bool position_claimed = false;
    bool scale_claimed = false;
    bool rotation_claimed = false;
    bool opacity_claimed = false;

    for (const auto& track : layer.animation->tracks) {
        const auto easing = track_easing(track.easing);
        const auto component_track = [&](std::string_view prefix) {
            return track.property == std::string(prefix) + "_x" ||
                   track.property == std::string(prefix) + "_y" ||
                   track.property == std::string(prefix) + "_z";
        };
        if (track.property == "position" || component_track("position")) {
            if (position_claimed)
                throw std::runtime_error("layer '" + layer.id + "' has overlapping position tracks");
            position_claimed = true;
            auto& animated = builder.position_anim();
            animated.set(base_position);
            for (const auto& key : track.keyframes) {
                auto value = base_position;
                if (track.property == "position") {
                    value = vector_value(key, track.property, base_position);
                } else {
                    const float offset = scalar_value(key, track.property);
                    if (track.property == "position_x") value.x += offset;
                    else if (track.property == "position_y") value.y += offset;
                    else value.z += offset;
                }
                animated.add_keyframe(key.frame, value, easing);
            }
        } else if (track.property == "scale" || component_track("scale")) {
            if (scale_claimed)
                throw std::runtime_error("layer '" + layer.id + "' has overlapping scale tracks");
            scale_claimed = true;
            auto& animated = builder.scale_anim();
            animated.set(base_scale);
            for (const auto& key : track.keyframes) {
                auto value = base_scale;
                if (track.property == "scale") {
                    value = vector_value(key, track.property, base_scale);
                } else {
                    const float scalar = scalar_value(key, track.property);
                    if (track.property == "scale_x") value.x = scalar;
                    else if (track.property == "scale_y") value.y = scalar;
                    else value.z = scalar;
                }
                animated.add_keyframe(key.frame, value, easing);
            }
        } else if (track.property == "rotation" || component_track("rotation")) {
            if (rotation_claimed)
                throw std::runtime_error("layer '" + layer.id + "' has overlapping rotation tracks");
            rotation_claimed = true;
            auto& animated = builder.rotate_anim();
            animated.set(base_rotation);
            for (const auto& key : track.keyframes) {
                auto value = base_rotation;
                if (track.property == "rotation") {
                    value = vector_value(key, track.property, base_rotation);
                } else {
                    const float scalar = scalar_value(key, track.property);
                    if (track.property == "rotation_x") value.x = scalar;
                    else if (track.property == "rotation_y") value.y = scalar;
                    else value.z = scalar;
                }
                animated.add_keyframe(key.frame, value, easing);
            }
        } else if (track.property == "opacity") {
            if (opacity_claimed)
                throw std::runtime_error("layer '" + layer.id + "' has duplicate opacity tracks");
            opacity_claimed = true;
            auto& animated = builder.opacity_anim();
            animated.set(layer.opacity.value_or(1.0f));
            for (const auto& key : track.keyframes)
                animated.add_keyframe(key.frame, scalar_value(key, track.property), easing);
        } else {
            throw std::runtime_error("unsupported primitive animation property: " + track.property);
        }
    }
}

}  // namespace

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

void apply_layer_primitives(chronon3d::LayerBuilder& builder, const LayerPlan& layer) {
    if (layer.start_frame) builder.from(*layer.start_frame);
    if (layer.duration_frames) builder.duration(*layer.duration_frames);
    if (layer.position_dimensions >= 2)
        builder.position({layer.position[0], layer.position[1], layer.position[2]});
    if (layer.scale_dimensions >= 2)
        builder.scale({layer.scale[0], layer.scale[1], layer.scale[2]});
    if (layer.rotation_dimensions > 0)
        builder.rotate({layer.rotation[0], layer.rotation[1], layer.rotation[2]});
    if (layer.blend_mode) builder.blend(*layer.blend_mode);
    if (layer.opacity) builder.opacity(*layer.opacity);
    apply_animation_tracks(builder, layer);
}

}  // namespace chronon3d::render_plan::detail
