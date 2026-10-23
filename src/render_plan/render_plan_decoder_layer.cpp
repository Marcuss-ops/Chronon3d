#include "render_plan_decoder_detail.hpp"
#include <chronon3d/render_plan/easing_contract.hpp>
#include "json_vector.hpp"
#include <chronon3d/render_plan/animation_property_registry.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>
#include <chronon3d/effects/effect_ids.hpp>

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
#include <unordered_set>

namespace chronon3d::render_plan::detail {
namespace {

template <typename T>
std::optional<T> optional_value(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) return std::nullopt;
    return object.at(key).get<T>();
}


void decode_light_fields(LayerPlan& layer, const nlohmann::json& value) {
    if (!value.contains("light")) return;
    if (layer.type != LayerType::Light)
        throw std::runtime_error("light is only valid for light layers");
    const auto& raw = value.at("light");
    LightLayerPlan light;
    light.radius = raw.value("radius", light.radius);
    light.intensity = raw.value("intensity", light.intensity);
    const auto color = parse_hex_color(raw.value("color", std::string{"#FFFFFF"}));
    if (!color) throw std::runtime_error("light.color must be a #RRGGBB or #RRGGBBAA color");
    light.color = {color->r, color->g, color->b, color->a};
    if (!std::isfinite(light.radius) || light.radius <= 0.0f ||
        !std::isfinite(light.intensity) || light.intensity < 0.0f || light.intensity > 16.0f)
        throw std::runtime_error("light radius must be positive and intensity must be in [0, 16]");
    if (raw.contains("kind")) {
        const auto kind_name = raw.at("kind").get<std::string>();
        if (kind_name == "radial") light.kind = LightKindPlan::Radial;
        else if (kind_name == "point") light.kind = LightKindPlan::Point;
        else if (kind_name == "spot") light.kind = LightKindPlan::Spot;
        else if (kind_name == "directional") light.kind = LightKindPlan::Directional;
        else throw std::runtime_error("light.kind must be radial, point, spot, or directional");
    }
    if (raw.contains("direction")) {
        const auto& direction = raw.at("direction");
        if (!direction.is_array() || direction.size() != 3)
            throw std::runtime_error("light.direction must contain three numbers");
        for (std::size_t i = 0; i < 3; ++i) light.direction[i] = direction.at(i).get<float>();
    }
    light.range = raw.value("range", light.range);
    light.decay = raw.value("decay", light.decay);
    light.angle = raw.value("angle", light.angle);
    light.penumbra = raw.value("penumbra", light.penumbra);
    light.cast_shadow = raw.value("cast_shadow", light.cast_shadow);
    light.shadow_radius = raw.value("shadow_radius", light.shadow_radius);
    light.shadow_bias = raw.value("shadow_bias", light.shadow_bias);
    const bool direction_finite = std::all_of(light.direction.begin(), light.direction.end(),
        [](float component) { return std::isfinite(component); });
    const float direction_len2 = light.direction[0] * light.direction[0] +
        light.direction[1] * light.direction[1] + light.direction[2] * light.direction[2];
    if (!direction_finite || (light.kind != LightKindPlan::Radial && !(direction_len2 > 0.0f)))
        throw std::runtime_error("light.direction must be finite and non-zero for spot and directional lights");
    if (!std::isfinite(light.range) || light.range < 0.0f || light.range > 1000000.0f)
        throw std::runtime_error("light.range must be finite and in [0, 1000000]; 0 means unbounded");
    if (!std::isfinite(light.decay) || light.decay < 0.0f || light.decay > 8.0f)
        throw std::runtime_error("light.decay must be finite and in [0, 8]");
    if (!std::isfinite(light.angle) || light.angle <= 0.0f || light.angle > 3.14159265358979323846f)
        throw std::runtime_error("light.angle must be finite and in (0, pi] radians");
    if (!std::isfinite(light.penumbra) || light.penumbra < 0.0f || light.penumbra > 1.0f)
        throw std::runtime_error("light.penumbra must be finite and in [0, 1]");
    if (!std::isfinite(light.shadow_radius) || light.shadow_radius < 0.0f || light.shadow_radius > 512.0f)
        throw std::runtime_error("light.shadow_radius must be finite and in [0, 512]");
    if (!std::isfinite(light.shadow_bias) || light.shadow_bias < 0.0f || light.shadow_bias > 16.0f)
        throw std::runtime_error("light.shadow_bias must be finite and in [0, 16]");
    if (light.cast_shadow && light.kind == LightKindPlan::Radial)
        throw std::runtime_error("light.cast_shadow requires a point, spot, or directional light");
    layer.light = light;
    if (!value.contains("blend_mode")) layer.blend_mode = BlendMode::Screen;

}
void decode_particle_fields(LayerPlan& layer, const nlohmann::json& value) {
    if (!value.contains("particle_emitter")) return;
    if (layer.type != LayerType::Particles)
        throw std::runtime_error("particle_emitter is only valid for particles layers");
    const auto& raw = value.at("particle_emitter");
    ParticleEmitterParams params;
    params.count = raw.at("count").get<u32>();
    params.seed = raw.at("seed").get<u32>();
    const auto read_vec2 = [](const nlohmann::json& object, const char* key) {
        const auto input = decode_json_vector<2>(object.at(key),
            std::string("particle_emitter.") + key + " must have two numbers");
        const Vec2 result{input[0], input[1]};
        if (!std::isfinite(result.x) || !std::isfinite(result.y))
            throw std::runtime_error(std::string("particle_emitter.") + key + " must be finite");
        return result;
    };
    params.emitter_size = read_vec2(raw, "emitter_size");
    params.velocity_min = read_vec2(raw, "velocity_min");
    params.velocity_max = read_vec2(raw, "velocity_max");
    const auto& lifetime = raw.at("lifetime");
    const auto& size = raw.at("size");
    if (!lifetime.is_array() || lifetime.size() != 2 || !size.is_array() || size.size() != 2)
        throw std::runtime_error("particle_emitter lifetime and size must be [min, max]");
    params.lifetime_min = lifetime[0].get<float>();
    params.lifetime_max = lifetime[1].get<float>();
    params.size_min = size[0].get<float>();
    params.size_max = size[1].get<float>();
    params.gravity = raw.value("gravity", 0.0f);
    params.turbulence = raw.value("turbulence", 0.0f);
    params.environment = raw.value("environment", false);
    params.bokeh = raw.value("bokeh", false);
    if (raw.contains("depth_range")) {
        const auto& range = raw.at("depth_range");
        if (!range.is_array() || range.size() != 2)
            throw std::runtime_error("particle_emitter.depth_range must be [min, max]");
        params.depth_min = range[0].get<float>();
        params.depth_max = range[1].get<float>();
    }
    const auto shape_name = raw.value("shape", std::string{"box"});
    if (shape_name == "ellipse") params.shape = EmitterShape::Ellipse;
    else if (shape_name == "edge") params.shape = EmitterShape::Edge;
    else if (shape_name == "path") params.shape = EmitterShape::Path;
    else if (shape_name != "box") throw std::runtime_error("particle emitter shape must be box, ellipse, edge, or path");
    params.path_id = raw.value("path_id", std::string{});
    params.sprite_asset = raw.value("sprite_asset", std::string{});
    if (raw.contains("blend_mode")) {
        const auto mode=blend_mode(raw.at("blend_mode").get<std::string>());
        if (!mode) throw std::runtime_error("particle emitter blend_mode is unsupported");
        params.blend_mode=*mode;
    }
    if (raw.contains("sprite_scale")) {
        const auto& range = raw.at("sprite_scale");
        if (!range.is_array() || range.size()!=2) throw std::runtime_error("particle sprite_scale must be [min, max]");
        params.sprite_scale_min=range[0].get<float>(); params.sprite_scale_max=range[1].get<float>();
    }
    if (raw.contains("rotation")) {
        const auto& range = raw.at("rotation");
        if (!range.is_array() || range.size()!=2) throw std::runtime_error("particle rotation must be [min, max]");
        params.rotation_min=range[0].get<float>(); params.rotation_max=range[1].get<float>();
    }
    if (raw.contains("angular_velocity")) {
        const auto& range = raw.at("angular_velocity");
        if (!range.is_array() || range.size()!=2) throw std::runtime_error("particle angular_velocity must be [min, max]");
        params.angular_velocity_min=range[0].get<float>(); params.angular_velocity_max=range[1].get<float>();
    }
    if (raw.contains("path_points")) {
        for (const auto& point : raw.at("path_points")) {
            if (!point.is_array() || point.size()!=2) throw std::runtime_error("particle path_points entries need two values");
            params.path_points.push_back({point[0].get<float>(),point[1].get<float>()});
        }
    }
    if (raw.contains("color_ramp")) {
        std::vector<graphics::GradientStop> stops;
        for (const auto& stop : raw.at("color_ramp")) {
            const auto& color = stop.at("color");
            if (!color.is_array() || color.size() != 4)
                throw std::runtime_error("particle color ramp stops require RGBA");
            const float position = stop.at("position").get<float>();
            const Color decoded{color[0].get<float>(), color[1].get<float>(),
                                color[2].get<float>(), color[3].get<float>()};
            const auto valid = [](float component) {
                return std::isfinite(component) && component >= 0.0f && component <= 1.0f;
            };
            if (!valid(position) || !valid(decoded.r) || !valid(decoded.g) ||
                !valid(decoded.b) || !valid(decoded.a))
                throw std::runtime_error("particle color ramp values must be finite and in [0, 1]");
            stops.push_back({position, decoded});
        }
        if (stops.size() < 2)
            throw std::runtime_error("particle color ramp requires at least two stops");
        params.color_ramp = graphics::GradientDefinition::linear(
            {0.0f, 0.0f}, {1.0f, 0.0f}, std::move(stops));
    }
    if (const auto error = validate_particle_emitter(params); !error.empty())
        throw std::runtime_error(error);
    layer.particle_emitter = std::move(params);

}
void decode_layer_style_fields(LayerPlan& layer, const nlohmann::json& value) {
    if (value.contains("style")) {
        const auto& style = value.at("style");
        LayerStylePlan style_plan;
        style_plan.font = style.value("font", std::string{});
        style_plan.font_family = style.value("font_family", std::string{});
        style_plan.font_weight = style.value("font_weight", 400);
        style_plan.font_style = style.value("font_style", std::string{"normal"});
        style_plan.text_direction = style.value("direction", std::string{"auto"});
        style_plan.text_align = style.value("text_align", std::string{"center"});
        style_plan.font_size = optional_value<float>(style, "font_size");
        style_plan.fill = style.value("fill", std::string{});
        style_plan.fill_opacity = optional_value<float>(style, "fill_opacity");
        style_plan.text_fit_mode = style.value("fit_mode", std::string{"none"});
        style_plan.min_font_size = optional_value<float>(style, "min_font_size");
        style_plan.max_font_size = optional_value<float>(style, "max_font_size");
        if (style_plan.font_weight < 100 || style_plan.font_weight > 900 ||
            style_plan.font_weight % 100 != 0 ||
            (style_plan.font_style != "normal" && style_plan.font_style != "italic" &&
             style_plan.font_style != "oblique") ||
            (style_plan.text_direction != "auto" && style_plan.text_direction != "ltr" &&
             style_plan.text_direction != "rtl") ||
            (style_plan.text_align != "left" && style_plan.text_align != "center" &&
             style_plan.text_align != "right"))
            throw std::runtime_error("text style font weight, style, or alignment is invalid");
        if (style_plan.text_fit_mode != "none" &&
            style_plan.text_fit_mode != "shrink_only" &&
            style_plan.text_fit_mode != "grow_and_shrink")
            throw std::runtime_error("text style fit_mode must be none, shrink_only, or grow_and_shrink");
        if ((style_plan.min_font_size &&
             (!std::isfinite(*style_plan.min_font_size) || *style_plan.min_font_size <= 0.0f)) ||
            (style_plan.max_font_size &&
             (!std::isfinite(*style_plan.max_font_size) || *style_plan.max_font_size <= 0.0f)) ||
            style_plan.max_font_size.value_or(160.0f) <
                style_plan.min_font_size.value_or(12.0f))
            throw std::runtime_error("text style font size limits must be positive, finite, and ordered");
        if (style_plan.fill_opacity &&
            (!std::isfinite(*style_plan.fill_opacity) || *style_plan.fill_opacity < 0.0f ||
             *style_plan.fill_opacity > 1.0f))
            throw std::runtime_error("text style fill_opacity must be finite and in [0, 1]");
        if (style_plan.text_fit_mode != "none" && layer.type != LayerType::Text)
            throw std::runtime_error("style.fit_mode is only valid for text layers");
        if ((style_plan.min_font_size || style_plan.max_font_size) &&
            style_plan.text_fit_mode == "none")
            throw std::runtime_error("text font size limits require a non-none fit_mode");
        if (style.contains("stroke")) {
            const auto& stroke = style.at("stroke");
            style_plan.stroke = StrokeStyle{stroke.value("color", std::string{}),
                                            optional_value<float>(stroke, "width"),
                                            std::nullopt, std::nullopt, std::nullopt};
            style_plan.stroke->opacity = optional_value<float>(stroke, "opacity");
            if (style_plan.stroke->opacity &&
                (!std::isfinite(*style_plan.stroke->opacity) ||
                 *style_plan.stroke->opacity < 0.0f || *style_plan.stroke->opacity > 1.0f))
                throw std::runtime_error("text style stroke opacity must be finite and in [0, 1]");
        }
        if (style.contains("shadow") && style.contains("shadows"))
            throw std::runtime_error("style.shadow and style.shadows are mutually exclusive");
        const auto decode_shadow = [](const nlohmann::json& shadow) {
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
            return shadow_plan;
        };
        if (style.contains("shadow")) {
            style_plan.shadow = decode_shadow(style.at("shadow"));
        }
        if (style.contains("shadows")) {
            const auto& shadows = style.at("shadows");
            if (!shadows.is_array() || shadows.size() > 3)
                throw std::runtime_error("style.shadows must contain at most 3 shadows");
            for (const auto& raw_shadow : shadows) {
                auto shadow = decode_shadow(raw_shadow);
                const float opacity = shadow.opacity.value_or(1.0f);
                const float blur = shadow.blur.value_or(0.0f);
                if (!std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f ||
                    !std::isfinite(blur) || blur < 0.0f ||
                    !std::isfinite(shadow.offset[0]) || !std::isfinite(shadow.offset[1]) ||
                    shadow.offset_dimensions != 2 || shadow.color.empty())
                    throw std::runtime_error("style.shadows entries require color, finite offset, opacity in [0,1], and non-negative blur");
                style_plan.shadows.push_back(std::move(shadow));
            }
        }
        if (style.contains("backdrop")) {
            if (layer.type != LayerType::Shape)
                throw std::runtime_error("style.backdrop is only valid for shape layers");
            const auto& raw_backdrop = style.at("backdrop");
            BackdropBlurStyle backdrop;
            backdrop.blur = raw_backdrop.at("blur").get<float>();
            backdrop.brightness = raw_backdrop.value("brightness", 1.0f);
            backdrop.saturation = raw_backdrop.value("saturation", 1.0f);
            backdrop.rim_light = raw_backdrop.value("rim_light", 0.0f);
            backdrop.refraction = raw_backdrop.value("refraction", 0.0f);
            backdrop.surface_noise = raw_backdrop.value("surface_noise", 0.0f);
            if (!std::isfinite(backdrop.blur) || backdrop.blur < 0.0f || backdrop.blur > 64.0f ||
                !std::isfinite(backdrop.brightness) || backdrop.brightness < 0.0f || backdrop.brightness > 4.0f ||
                !std::isfinite(backdrop.saturation) || backdrop.saturation < 0.0f || backdrop.saturation > 4.0f ||
                !std::isfinite(backdrop.rim_light) || backdrop.rim_light < 0.0f || backdrop.rim_light > 2.0f ||
                !std::isfinite(backdrop.refraction) || backdrop.refraction < 0.0f || backdrop.refraction > 4.0f ||
                !std::isfinite(backdrop.surface_noise) || backdrop.surface_noise < 0.0f || backdrop.surface_noise > 0.1f)
                throw std::runtime_error("style.backdrop controls are non-finite or outside supported bounds");
            style_plan.backdrop = backdrop;
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


}

}  // namespace

LayerPlan decode_layer(const nlohmann::json& value) {
    LayerPlan layer;
    layer.id = value.at("id").get<std::string>();
    layer.type = layer_type(value.at("type").get<std::string>());
    decode_layer_follow_path(layer, value);
    decode_layer_layout(layer, value);
    if (value.contains("transition_in")) layer.transition_in = decode_transition(value.at("transition_in"));
    if (value.contains("transition_out")) layer.transition_out = decode_transition(value.at("transition_out"));
    decode_light_fields(layer, value);
    decode_particle_fields(layer, value);
    if (value.contains("parent"))
        layer.parent = value.at("parent").get<std::string>();
    decode_layer_timing(layer, value);
    decode_layer_asset_fields(layer, value);
    layer.text = value.value("text", std::string{});
    decode_layer_text_counter(layer, value);
    decode_layer_text_spans(layer, value);
    decode_layer_size_color_fields(layer, value);

    decode_shape_and_fill_gradient(layer, value);

    decode_layer_masks(layer, value);
    decode_layer_effects(layer, value);
    decode_background_look(layer, value);
    decode_layer_backdrop_filter(layer, value);
    decode_effect_parameter_tracks(layer, value);
    validate_adjustment_layer(layer, value);
    decode_layer_transform_fields(layer, value);
    decode_layer_render_flags(layer, value);
    decode_layer_frame_crop_fields(layer, value);

    decode_layer_style_fields(layer, value);

    decode_layer_animation(layer, value);
    validate_layer_animation_fields(layer, value);
    decode_layer_motion_blur(layer, value);
    decode_layer_final_fields(layer, value);
    return layer;
}

}  // namespace chronon3d::render_plan::detail
