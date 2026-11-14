#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <numbers>
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
        style_plan.variation_axes = style.value("variation_axes", std::string{});
        style_plan.text_direction = style.value("direction", std::string{"auto"});
        style_plan.text_align = style.value("text_align", std::string{"center"});
        style_plan.font_size = optional_value<float>(style, "font_size");
        style_plan.tracking = optional_value<float>(style, "tracking");
        if (style_plan.tracking &&
            (!std::isfinite(*style_plan.tracking) ||
             *style_plan.tracking < -1000.0f ||
             *style_plan.tracking > 1000.0f))
            throw std::runtime_error(
                "text style tracking must be finite in [-1000, 1000]");
        if (style_plan.tracking && layer.type != LayerType::Text)
            throw std::runtime_error(
                "style.tracking is only valid for text layers");
        style_plan.fill = style.value("fill", std::string{});
        style_plan.quality = style.value("quality", std::string{"fast"});
        if (style_plan.quality != "fast" && style_plan.quality != "high")
            throw std::runtime_error("text style quality must be fast or high");
        style_plan.shadow_quality = style.value("shadow_quality", std::string{"fast"});
        if (style_plan.shadow_quality != "fast" &&
            style_plan.shadow_quality != "canonical")
            throw std::runtime_error("text style shadow_quality must be fast or canonical");
        style_plan.stroke_mode = style.value("stroke_mode", std::string{"auto"});
        if (style_plan.stroke_mode != "auto" &&
            style_plan.stroke_mode != "distance" &&
            style_plan.stroke_mode != "vector")
            throw std::runtime_error("text style stroke_mode must be auto, distance, or vector");
        if (layer.type != LayerType::Text &&
            (style.contains("quality") || style.contains("shadow_quality") ||
             style.contains("stroke_mode")))
            throw std::runtime_error(
                "style.quality, shadow_quality, and stroke_mode are only valid for text layers");
        style_plan.gel_surface = style.value("gel_surface", false);
        style_plan.gel_soft_fusion = optional_value<bool>(style, "gel_soft_fusion");
        style_plan.extruded_text = style.value("extruded_text", false);
        style_plan.extrusion_depth = style.value("extrusion_depth", 12.0f);
        style_plan.gel_transmission_enabled = style.value("gel_transmission", true);
        style_plan.gel_material_profile =
            style.value("gel_material_profile", std::string{"current"});
        style_plan.gel_core = style.value("gel_core", false);
        style_plan.gel_debug_mode = style.value("gel_debug_mode", std::uint8_t{0});
        if (style.contains("gel_light")) {
            const auto& light = style.at("gel_light");
            if (!light.is_object())
                throw std::runtime_error("text style gel_light must be an object");
            style_plan.gel_light_azimuth_radians =
                optional_value<float>(light, "azimuth_radians");
            style_plan.gel_light_elevation = optional_value<float>(light, "elevation");
        }
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
        if (style.contains("variation_axes")) {
            if (layer.type != LayerType::Text)
                throw std::runtime_error("style.variation_axes is only valid for text layers");
            if (style_plan.variation_axes.size() > 512)
                throw std::runtime_error("text style variation_axes is too long");
            std::size_t axis_begin = 0;
            while (axis_begin <= style_plan.variation_axes.size()) {
                const auto axis_end = style_plan.variation_axes.find(',', axis_begin);
                const auto token = style_plan.variation_axes.substr(
                    axis_begin, axis_end == std::string::npos
                        ? std::string::npos : axis_end - axis_begin);
                if (token.size() < 6 || token[4] != '=')
                    throw std::runtime_error(
                        "text style variation_axes requires tag=value entries");
                for (std::size_t i = 0; i < 4; ++i)
                    if (!std::isalnum(static_cast<unsigned char>(token[i])))
                        throw std::runtime_error(
                            "text style variation_axes tag must be four alphanumeric characters");
                char* end = nullptr;
                const float value = std::strtof(token.c_str() + 5, &end);
                if (end == token.c_str() + 5 || *end != '\0' || !std::isfinite(value))
                    throw std::runtime_error(
                        "text style variation_axes value must be finite");
                if (axis_end == std::string::npos) break;
                axis_begin = axis_end + 1;
            }
        }
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
        if ((style_plan.gel_surface || style_plan.gel_core) && layer.type != LayerType::Text)
            throw std::runtime_error("style.gel_surface is only valid for text layers");
        if (style_plan.gel_soft_fusion && !style_plan.gel_surface)
            throw std::runtime_error(
                "style.gel_soft_fusion requires gel_surface on a text layer");
        if (style_plan.gel_material_profile != "current" &&
            style_plan.gel_material_profile != "reference_20261107")
            throw std::runtime_error(
                "text style gel_material_profile must be current or reference_20261107");
        if (style_plan.gel_material_profile != "current" &&
            (!style_plan.gel_surface || layer.type != LayerType::Text))
            throw std::runtime_error(
                "style.gel_material_profile requires gel_surface on a text layer");
        if (style_plan.extruded_text &&
            (layer.type != LayerType::Text || !layer.enable_3d ||
             style_plan.gel_surface || style_plan.gel_core))
            throw std::runtime_error(
                "style.extruded_text requires a 3D text layer and is separate from gel_surface/gel_core");
        if (style_plan.extruded_text &&
            (!std::isfinite(style_plan.extrusion_depth) ||
             style_plan.extrusion_depth <= 0.0f ||
             style_plan.extrusion_depth > 10000.0f))
            throw std::runtime_error("style.extrusion_depth must be finite and in (0, 10000]");
        if (style_plan.gel_debug_mode > 7 ||
            (style_plan.gel_debug_mode != 0 &&
             (!style_plan.gel_surface || layer.type != LayerType::Text)))
            throw std::runtime_error(
                "style.gel_debug_mode must be 0..7 and requires gel_surface on text");
        if ((style.contains("gel_light") || style_plan.gel_light_azimuth_radians ||
             style_plan.gel_light_elevation) &&
            (!style_plan.gel_surface || layer.type != LayerType::Text))
            throw std::runtime_error("style.gel_light requires gel_surface on a text layer");
        constexpr float kPi = std::numbers::pi_v<float>;
        if ((style_plan.gel_light_azimuth_radians &&
             (!std::isfinite(*style_plan.gel_light_azimuth_radians) ||
              *style_plan.gel_light_azimuth_radians < -kPi ||
              *style_plan.gel_light_azimuth_radians > kPi)) ||
            (style_plan.gel_light_elevation &&
             (!std::isfinite(*style_plan.gel_light_elevation) ||
              *style_plan.gel_light_elevation < 0.0f ||
              *style_plan.gel_light_elevation > 1.0f)))
            throw std::runtime_error("text style gel_light values are out of range");
        if ((style_plan.min_font_size || style_plan.max_font_size) &&
            style_plan.text_fit_mode == "none")
            throw std::runtime_error("text font size limits require a non-none fit_mode");
        if (style.contains("wear")) {
            if (layer.type != LayerType::Text)
                throw std::runtime_error("style.wear is only valid for text layers");
            const auto& raw_wear = style.at("wear");
            if (!raw_wear.is_object())
                throw std::runtime_error("text style wear must be an object");
            TextWearStyle wear;
            wear.amount = raw_wear.value("amount", 0.5f);
            wear.scale = raw_wear.value("scale", 8.0f);
            wear.seed = raw_wear.value("seed", std::uint32_t{0});
            wear.softness = raw_wear.value("softness", 0.25f);
            if (!std::isfinite(wear.amount) || wear.amount < 0.0f || wear.amount > 1.0f ||
                !std::isfinite(wear.scale) || wear.scale <= 0.0f || wear.scale > 256.0f ||
                !std::isfinite(wear.softness) || wear.softness < 0.0f || wear.softness > 1.0f)
                throw std::runtime_error("text style wear controls are outside supported bounds");
            style_plan.wear = wear;
        }
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
            backdrop.light_wrap = raw_backdrop.value("light_wrap", 0.0f);
            if (!std::isfinite(backdrop.blur) || backdrop.blur < 0.0f || backdrop.blur > 64.0f ||
                !std::isfinite(backdrop.brightness) || backdrop.brightness < 0.0f || backdrop.brightness > 4.0f ||
                !std::isfinite(backdrop.saturation) || backdrop.saturation < 0.0f || backdrop.saturation > 4.0f ||
                !std::isfinite(backdrop.rim_light) || backdrop.rim_light < 0.0f || backdrop.rim_light > 2.0f ||
                !std::isfinite(backdrop.refraction) || backdrop.refraction < 0.0f || backdrop.refraction > 4.0f ||
                !std::isfinite(backdrop.surface_noise) || backdrop.surface_noise < 0.0f || backdrop.surface_noise > 0.1f ||
                !std::isfinite(backdrop.light_wrap) || backdrop.light_wrap < 0.0f || backdrop.light_wrap > 4.0f)
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
        if (style.contains("text_caret")) {
            if (layer.type != LayerType::Text)
                throw std::runtime_error("style.text_caret is only valid for text layers");
            const auto& caret = style.at("text_caret");
            if (!caret.is_object())
                throw std::runtime_error("style.text_caret must be an object");
            TextCaretPlan plan;
            if (caret.contains("byte_index")) {
                if (!caret.at("byte_index").is_number_integer())
                    throw std::runtime_error("style.text_caret.byte_index must be an integer");
                const auto index = caret.at("byte_index").get<std::int64_t>();
                if (index < 0 || index > 1000000)
                    throw std::runtime_error("style.text_caret.byte_index must be in [0, 1000000]");
                plan.byte_index = static_cast<std::size_t>(index);
            }
            plan.width = caret.value("width", 2.0f);
            plan.height = caret.value("height", 0.0f);
            plan.color = caret.value("color", std::string{"#FFFFFF"});
            plan.opacity = caret.value("opacity", 1.0f);
            plan.blink_period_frames = caret.value("blink_period_frames", 0);
            plan.blink_duty = caret.value("blink_duty", 0.5f);
            if (!std::isfinite(plan.width) || plan.width <= 0.0f || plan.width > 256.0f ||
                !std::isfinite(plan.height) || plan.height < 0.0f || plan.height > 4096.0f ||
                !std::isfinite(plan.opacity) || plan.opacity < 0.0f || plan.opacity > 1.0f ||
                !std::isfinite(plan.blink_duty) || plan.blink_duty < 0.0f || plan.blink_duty > 1.0f ||
                plan.blink_period_frames < 0 || plan.blink_period_frames > 1000000 ||
                plan.color.size() < 7)
                throw std::runtime_error("style.text_caret values are outside supported bounds");
            style_plan.text_caret = std::move(plan);
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
