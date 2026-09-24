#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <optional>
#include <stdexcept>
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
    // 'property' is optional: text-selector bounds (start/end/offset/amount)
    // carry it for backward compatibility but never use it semantically, so a
    // selector track without a property is valid.  Layer-animation and text-
    // animator property tracks require it at the schema level; a missing one
    // surfaces as "unsupported property" during compilation instead of a
    // confusing decode failure.
    track.property = value.value("property", std::string{});
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

    if (value.contains("shape")) {
        const auto& raw_shape = value.at("shape");
        ShapeContentPlan shape;
        const auto shape_type = raw_shape.at("type").get<std::string>();
        if (shape_type == "rect") shape.type = ShapeContentTypePlan::Rect;
        else if (shape_type == "rounded_rect") shape.type = ShapeContentTypePlan::RoundedRect;
        else if (shape_type == "ellipse") shape.type = ShapeContentTypePlan::Ellipse;
        else if (shape_type == "line") shape.type = ShapeContentTypePlan::Line;
        else if (shape_type == "path") shape.type = ShapeContentTypePlan::Path;
        else throw std::runtime_error("unsupported shape.type: " + shape_type);
        shape.radius = raw_shape.value("radius", 0.0f);
        if (raw_shape.contains("path")) {
            for (const auto& raw_command : raw_shape.at("path")) {
                ShapePathCommandPlan command;
                const auto kind = raw_command.at("type").get<std::string>();
                if (kind == "move_to") command.type = ShapePathCommandTypePlan::MoveTo;
                else if (kind == "line_to") command.type = ShapePathCommandTypePlan::LineTo;
                else if (kind == "quadratic_to") command.type = ShapePathCommandTypePlan::QuadraticTo;
                else if (kind == "cubic_to") command.type = ShapePathCommandTypePlan::CubicTo;
                else if (kind == "close") command.type = ShapePathCommandTypePlan::Close;
                else throw std::runtime_error("unsupported shape path command type: " + kind);
                const auto read_point = [](const nlohmann::json& point,
                                           std::array<float, 2>& output) {
                    if (!point.is_array() || point.size() != 2)
                        throw std::runtime_error("shape path points must have exactly two numbers");
                    output = {point[0].get<float>(), point[1].get<float>()};
                };
                if (raw_command.contains("point")) read_point(raw_command.at("point"), command.point);
                if (raw_command.contains("control1")) read_point(raw_command.at("control1"), command.control1);
                if (raw_command.contains("control2")) read_point(raw_command.at("control2"), command.control2);
                shape.path.push_back(command);
            }
        }
        if (raw_shape.contains("fill")) {
            const auto& fill = raw_shape.at("fill");
            if (fill.is_array()) {
                if (fill.size() != 4)
                    throw std::runtime_error("shape.fill must contain RGBA");
                shape.fill_color = std::array<float, 4>{
                    fill[0].get<float>(), fill[1].get<float>(),
                    fill[2].get<float>(), fill[3].get<float>()};
            } else if (fill.is_object()) {
                shape.gradient = decode_gradient_definition(fill);
            }
        }
        if (raw_shape.contains("stroke")) {
            const auto& stroke = raw_shape.at("stroke");
            StrokeStyle decoded_stroke;
            decoded_stroke.color = "#FFFFFF";
            if (stroke.contains("color")) {
                const auto& stroke_color = stroke.at("color");
                if (stroke_color.is_array()) {
                    if (stroke_color.size() != 4)
                        throw std::runtime_error("shape.stroke.color must contain RGBA");
                    decoded_stroke.solid_color = std::array<float, 4>{
                        stroke_color[0].get<float>(), stroke_color[1].get<float>(),
                        stroke_color[2].get<float>(), stroke_color[3].get<float>()};
                } else if (stroke_color.is_string()) {
                    decoded_stroke.color = stroke_color.get<std::string>();
                } else if (stroke_color.is_object()) {
                    decoded_stroke.gradient = decode_gradient_definition(stroke_color);
                }
            }
            if (stroke.contains("width")) decoded_stroke.width = stroke.at("width").get<float>();
            if (stroke.contains("trim_start")) decoded_stroke.trim_start = stroke.at("trim_start").get<float>();
            if (stroke.contains("trim_end")) decoded_stroke.trim_end = stroke.at("trim_end").get<float>();
            if (stroke.contains("gradient"))
                decoded_stroke.gradient = decode_gradient_definition(stroke.at("gradient"));
            shape.stroke = std::move(decoded_stroke);
        }
        layer.shape = std::move(shape);
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
    layer.enable_3d = value.value("enable_3d", false);
    layer.screen_space = value.value("screen_space", false);
    if (value.contains("depth_role")) {
        layer.depth_role = value.at("depth_role").get<std::string>();
        static constexpr std::array<std::string_view, 7> kRoles{
            "far_background", "background", "midground", "subject",
            "atmosphere", "foreground", "overlay"};
        if (std::find(kRoles.begin(), kRoles.end(), *layer.depth_role) == kRoles.end())
            throw std::runtime_error(
                "unsupported depth_role '" + *layer.depth_role +
                "'; valid roles: far_background, background, midground, subject, atmosphere, foreground, overlay");
    }
    if (value.contains("depth_offset"))
        layer.depth_offset = value.at("depth_offset").get<float>();

    layer.start_frame = optional_frame(value, "start_frame");
    layer.duration_frames = optional_frame(value, "duration_frames");
    if (value.contains("fit")) layer.fit = fit_mode(value.at("fit").get<std::string>());
    layer.radius = optional_value<float>(value, "radius");

    if (value.contains("style")) {
        const auto& style = value.at("style");
        LayerStylePlan style_plan;
        style_plan.font = style.value("font", std::string{});
        style_plan.font_size = optional_value<float>(style, "font_size");
        style_plan.fill = style.value("fill", std::string{});
        if (style.contains("stroke")) {
            const auto& stroke = style.at("stroke");
            style_plan.stroke = StrokeStyle{stroke.value("color", std::string{}),
                                            optional_value<float>(stroke, "width"),
                                            std::nullopt, std::nullopt, std::nullopt};
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
        if (style.contains("glow")) {
            const auto& glow = style.at("glow");
            GlowStyle glow_plan;
            if (glow.contains("radius"))
                glow_plan.radius = glow.at("radius").get<float>();
            if (glow.contains("intensity"))
                glow_plan.intensity = glow.at("intensity").get<float>();
            if (glow.contains("color")) {
                glow_plan.color = glow.at("color").get<std::string>();
            } else {
                // Legacy plans declared the glow tint implicitly as white.
                // Old lobe and quality knobs are accepted but ignored.
                glow_plan.color = "#FFFFFF";
            }
            if (!std::isfinite(glow_plan.radius) || glow_plan.radius < 0.0f ||
                !std::isfinite(glow_plan.intensity) || glow_plan.intensity < 0.0f) {
                throw std::runtime_error(
                    "style.glow radius and intensity must be finite and non-negative");
            }
            style_plan.glow = std::move(glow_plan);
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
