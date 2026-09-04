#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {
namespace {

template <typename T>
std::optional<T> optional_value(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return object.at(key).get<T>();
}

std::optional<Frame> optional_frame(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return Frame{object.at(key).get<std::int64_t>()};
}

AnimationTrackPlan decode_animation_track(const nlohmann::json& value) {
    AnimationTrackPlan track;
    track.property = value.at("property").get<std::string>();
    track.easing = value.value("easing", std::string{"linear"});
    for (const auto& key_value : value.at("keyframes")) {
        AnimationKeyframePlan key;
        key.frame = Frame{key_value.at("frame").get<std::int64_t>()};
        const auto& raw = key_value.at("value");
        if (raw.is_array()) {
            for (const auto& component : raw) key.value.push_back(component.get<float>());
        } else {
            key.value.push_back(raw.get<float>());
        }
        track.keyframes.push_back(std::move(key));
    }
    return track;
}

TextSelectorPlan decode_text_selector(const nlohmann::json& value) {
    TextSelectorPlan selector;
    selector.id = value.value("id", std::string{});
    selector.unit = value.value("unit", std::string{"glyph"});
    selector.shape = value.value("shape", std::string{"smooth"});
    selector.order = value.value("order", std::string{"forward"});
    selector.combine = value.value("combine", std::string{"replace"});
    if (value.contains("start")) selector.start = decode_animation_track(value.at("start"));
    if (value.contains("end")) selector.end = decode_animation_track(value.at("end"));
    if (value.contains("offset")) selector.offset = decode_animation_track(value.at("offset"));
    if (value.contains("amount")) selector.amount = decode_animation_track(value.at("amount"));
    selector.exclude_spaces = value.value("exclude_spaces", true);
    selector.randomize_order = value.value("randomize_order", false);
    selector.random_seed = value.value("random_seed", std::uint64_t{0});
    return selector;
}

TextAnimatorPlan decode_text_animator(const nlohmann::json& value) {
    TextAnimatorPlan animator;
    animator.id = value.value("id", std::string{});
    if (value.contains("selectors")) {
        for (const auto& selector : value.at("selectors"))
            animator.selectors.push_back(decode_text_selector(selector));
    }
    if (value.contains("properties")) {
        for (const auto& property : value.at("properties"))
            animator.properties.push_back(decode_animation_track(property));
    }
    return animator;
}

}  // namespace

LayerPlan decode_layer(const nlohmann::json& value) {
    LayerPlan layer;
    layer.id = value.at("id").get<std::string>();
    layer.type = layer_type(value.at("type").get<std::string>());
    layer.asset = value.value("asset", std::string{});
    layer.source = value.value("source", std::string{});
    layer.text = value.value("text", std::string{});

    if (value.contains("size")) {
        const auto& size = value.at("size");
        layer.size_dimensions = size.size();
        for (std::size_t i = 0; i < size.size() && i < layer.size.size(); ++i)
            layer.size[i] = size.at(i).get<float>();
    }
    if (value.contains("color")) {
        const auto& color = value.at("color");
        for (std::size_t i = 0; i < color.size() && i < layer.color.size(); ++i)
            layer.color[i] = color.at(i).get<float>();
    }
    if (value.contains("position")) {
        const auto& position = value.at("position");
        layer.position_dimensions = position.size();
        for (std::size_t i = 0; i < position.size() && i < layer.position.size(); ++i)
            layer.position[i] = position.at(i).get<float>();
    }
    if (value.contains("scale")) {
        const auto& scale = value.at("scale");
        layer.scale_dimensions = scale.size();
        for (std::size_t i = 0; i < scale.size() && i < layer.scale.size(); ++i)
            layer.scale[i] = scale.at(i).get<float>();
    }
    if (value.contains("rotation")) {
        const auto& rotation = value.at("rotation");
        layer.rotation_dimensions = rotation.size();
        for (std::size_t i = 0; i < rotation.size() && i < layer.rotation.size(); ++i)
            layer.rotation[i] = rotation.at(i).get<float>();
    }

    layer.start_frame = optional_frame(value, "start_frame");
    layer.duration_frames = optional_frame(value, "duration_frames");
    if (value.contains("fit")) layer.fit = fit_mode(value.at("fit").get<std::string>());

    if (value.contains("style")) {
        const auto& style = value.at("style");
        LayerStylePlan style_plan;
        style_plan.font = style.value("font", std::string{});
        style_plan.font_size = optional_value<float>(style, "font_size");
        style_plan.fill = style.value("fill", std::string{});
        if (style.contains("stroke")) {
            const auto& stroke = style.at("stroke");
            style_plan.stroke = StrokeStyle{stroke.value("color", std::string{}),
                                            optional_value<float>(stroke, "width")};
        }
        if (style.contains("shadow")) {
            const auto& shadow = style.at("shadow");
            ShadowStyle shadow_plan;
            shadow_plan.color = shadow.value("color", std::string{});
            shadow_plan.opacity = optional_value<float>(shadow, "opacity");
            shadow_plan.blur = optional_value<float>(shadow, "blur");
            if (shadow.contains("offset")) {
                const auto& offset = shadow.at("offset");
                shadow_plan.offset_dimensions = offset.size();
                for (std::size_t i = 0; i < offset.size() && i < shadow_plan.offset.size(); ++i)
                    shadow_plan.offset[i] = offset.at(i).get<float>();
            }
            style_plan.shadow = std::move(shadow_plan);
        }
        if (style.contains("background")) {
            const auto& background = style.at("background");
            BackgroundStyle background_plan;
            background_plan.color = background.value("color", std::string{});
            background_plan.opacity = optional_value<float>(background, "opacity");
            background_plan.radius = optional_value<float>(background, "radius");
            if (background.contains("padding")) {
                const auto& padding = background.at("padding");
                background_plan.padding_dimensions = padding.size();
                for (std::size_t i = 0; i < padding.size() && i < background_plan.padding.size(); ++i)
                    background_plan.padding[i] = padding.at(i).get<float>();
            }
            style_plan.background = std::move(background_plan);
        }
        layer.font = style_plan.font;
        layer.style = std::move(style_plan);
    }

    if (value.contains("animation")) {
        AnimationPlan animation;
        for (const auto& track_value : value.at("animation").at("tracks"))
            animation.tracks.push_back(decode_animation_track(track_value));
        layer.animation = std::move(animation);
    }
    if (value.contains("text_animators")) {
        for (const auto& animator : value.at("text_animators"))
            layer.text_animators.push_back(decode_text_animator(animator));
    }
    if (value.contains("blend_mode")) {
        if (const auto mode = blend_mode(value.at("blend_mode").get<std::string>()))
            layer.blend_mode = mode;
    }
    layer.opacity = optional_value<float>(value, "opacity");
    layer.loop = value.value("loop", false);
    return layer;
}

}  // namespace chronon3d::render_plan::detail
