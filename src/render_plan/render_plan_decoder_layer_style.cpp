#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
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
} // namespace

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

} // namespace chronon3d::render_plan::detail
