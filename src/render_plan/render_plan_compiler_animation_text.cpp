#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_animation_internal.hpp"
#include <chronon3d/text/text_run_builder.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
namespace chronon3d::render_plan::detail {
namespace {
using namespace std::literals;
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

    const AnimationTrackPlan* skew_track = nullptr;
    const AnimationTrackPlan* skew_axis_track = nullptr;
    for (const auto& track : plan.properties) {
        if (track.property == "skew") {
            if (skew_track) throw std::runtime_error("text animator has duplicate skew tracks");
            skew_track = &track;
        } else if (track.property == "skew_axis") {
            if (skew_axis_track) throw std::runtime_error("text animator has duplicate skew_axis tracks");
            skew_axis_track = &track;
        }
    }

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
        case AnimationPropertyKind::CharacterOffset: {
            const int offset = track.keyframes.empty() ? 0 :
                static_cast<int>(scalar_value(track.keyframes.front(), "character_offset"));
            animator.properties.push_back(chronon3d::CharacterOffsetProperty{offset});
            break;
        }
        default:
            throw std::runtime_error("unsupported text animator property: " + track.property);
        }
    }
    if (animator.selectors.empty() || animator.properties.empty())
        throw std::runtime_error("text animator '" + animator.id + "' requires selectors and properties");
    return animator;
}

}
void apply_text_animators(chronon3d::TextRunBuilder& builder, const LayerPlan& layer, FrameRate fps) {
    for (const auto& animator : layer.text_animators)
        builder.animator(compile_text_animator(animator, fps));
}

}
