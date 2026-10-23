#include "render_plan_compiler_detail.hpp"
#include "render_plan_compiler_lottie_detail.hpp"
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::render_plan::detail {
namespace { using json = nlohmann::json; }


// Solid fill/stroke paint: bounded static RGBA, or animated color/opacity tracks.
bool decode_lottie_solid_paint(const json& style,
                               LottieSolidPaint& paint,
                               std::string_view color_property,
                               std::string_view opacity_property) {
    if (style.value("r", 1) != 1 || !style.contains("c") ||
        !style["c"].is_object() || !style["c"].contains("k")) return false;
    std::array<float, 4> first_color{};
    const auto decode_rgba = [](const json& value, std::array<float, 4>& color) {
        if (!value.is_array() || value.size() != 4) return false;
        for (std::size_t i = 0; i < 4; ++i) {
            if (!value[i].is_number()) return false;
            color[i] = value[i].get<float>();
            if (!std::isfinite(color[i]) || color[i] < 0.0f || color[i] > 1.0f)
                return false;
        }
        return true;
    };
    const auto& color_property_json = style["c"];
    if (color_property_json.value("a", 0) == 1) {
        AnimationTrackPlan track;
        const std::string property_name{color_property};
        if (!lottie_decode_transform_track(style, "c", property_name.c_str(), 1.0f, track) ||
            track.keyframes.empty()) return false;
        for (const auto& key : track.keyframes) {
            if (key.value.size() != 4 || std::any_of(key.value.begin(), key.value.end(),
                    [](float component) {
                        return !std::isfinite(component) || component < 0.0f || component > 1.0f;
                    })) return false;
        }
        std::copy_n(track.keyframes.front().value.begin(), 4, first_color.begin());
        paint.color_animation = std::move(track);
    } else if (!decode_rgba(color_property_json["k"], first_color)) {
        return false;
    }
    float opacity = 100.0f;
    if (style.contains("o")) {
        if (!lottie_static_scalar_property(style, "o", opacity)) {
            AnimationTrackPlan track;
            const std::string property_name{opacity_property};
            if (!lottie_decode_transform_track(style, "o", property_name.c_str(), 100.0f, track) ||
                track.keyframes.empty()) return false;
            for (const auto& key : track.keyframes)
                if (key.value.size() != 1 || key.value[0] < 0.0f || key.value[0] > 1.0f)
                    return false;
            opacity = 100.0f;
            paint.opacity_animation = std::move(track);
        }
    }
    if (opacity < 0.0f || opacity > 100.0f) return false;
    if (!paint.opacity_animation) {
        first_color[3] *= opacity / 100.0f;
        if (paint.color_animation) {
            for (auto& key : paint.color_animation->keyframes)
                key.value[3] *= opacity / 100.0f;
        }
    }
    paint.color = first_color;
    return true;
}

// Linear/radial gradient definition with optional alpha stops.
bool decode_lottie_gradient(const json& style,
                            graphics::GradientDefinition& gradient) {
    if (style.value("r", 1) != 1 || !style.contains("g") ||
        !style["g"].is_object() || !style["g"].contains("p") ||
        !style["g"]["p"].is_number_integer() || !style["g"].contains("k") ||
        !style["g"]["k"].is_object() || style["g"]["k"].value("a", 1) != 0 ||
        !style["g"]["k"].contains("k")) return false;
    const auto count = style["g"]["p"].get<int>();
    const auto& raw_stops = style["g"]["k"]["k"];
    if (count < 2 || count > 128 || !raw_stops.is_array() ||
        raw_stops.size() < static_cast<std::size_t>(count) * 4 ||
        raw_stops.size() > static_cast<std::size_t>(count) * 4 + 256)
        return false;
    std::array<float, 2> start{}, end{};
    if (!lottie_static_vec2_property(style, "s", start) ||
        !lottie_static_vec2_property(style, "e", end)) return false;
    const auto kind = style.value("t", 1);
    if (kind != 1 && kind != 2) return false;
    float opacity = 100.0f;
    if (style.contains("o") && !lottie_static_scalar_property(style, "o", opacity)) return false;
    if (opacity < 0.0f || opacity > 100.0f) return false;
    std::vector<graphics::GradientStop> colors;
    colors.reserve(static_cast<std::size_t>(count));
    const auto bounded_unit = [](float value) {
        return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
    };
    for (int index = 0; index < count; ++index) {
        const auto offset = static_cast<std::size_t>(index) * 4;
        if (!raw_stops[offset].is_number() || !raw_stops[offset + 1].is_number() ||
            !raw_stops[offset + 2].is_number() || !raw_stops[offset + 3].is_number()) return false;
        const float position = raw_stops[offset].get<float>();
        const float red = raw_stops[offset + 1].get<float>();
        const float green = raw_stops[offset + 2].get<float>();
        const float blue = raw_stops[offset + 3].get<float>();
        if (!bounded_unit(position) || !bounded_unit(red) ||
            !bounded_unit(green) || !bounded_unit(blue)) return false;
        colors.push_back({position, Color{red, green, blue, opacity / 100.0f}});
    }
    std::vector<graphics::OpacityStop> opacity_stops;
    const auto opacity_values = raw_stops.size() - static_cast<std::size_t>(count) * 4;
    if (opacity_values % 2 != 0) return false;
    for (std::size_t offset = static_cast<std::size_t>(count) * 4;
         offset < raw_stops.size(); offset += 2) {
        if (!raw_stops[offset].is_number() || !raw_stops[offset + 1].is_number()) return false;
        const float position = raw_stops[offset].get<float>();
        const float value = raw_stops[offset + 1].get<float>();
        if (!bounded_unit(position) || !bounded_unit(value)) return false;
        opacity_stops.push_back({position, value * opacity / 100.0f});
    }
    const Vec2 from{start[0], start[1]}, to{end[0], end[1]};
    if (kind == 1) {
        gradient = graphics::GradientDefinition::linear(from, to, std::move(colors));
    } else {
        const float radius = glm::length(to - from);
        if (!std::isfinite(radius) || radius <= 1e-6f) return false;
        gradient = graphics::GradientDefinition::radial(from, radius, std::move(colors));
    }
    gradient.opacity_stops = std::move(opacity_stops);
    return true;
}


std::optional<std::string> decode_lottie_paints(
    const std::vector<const json*>& fills,
    const std::vector<const json*>& strokes,
    std::size_t geometry_count,
    LottiePaintSet& paints) {
    if (fills.size() != 1 && fills.size() != geometry_count)
        return std::string("Lottie supports one solid fill or one fill per static geometry");
    if (!strokes.empty() && strokes.size() != 1 && strokes.size() != geometry_count)
        return std::string("Lottie supports one solid stroke or one stroke per static geometry");
    if (fills.empty() && strokes.empty())
        return std::string("Lottie static geometry requires a solid fill or stroke");
        std::vector<std::optional<LottieSolidPaint>> fill_paints;
        std::vector<std::optional<graphics::GradientDefinition>> fill_gradients;
        for (const auto* fill : fills) {
            if (fill->value("ty", std::string{}) == "gf") {
                graphics::GradientDefinition gradient;
                if (!decode_lottie_gradient(*fill, gradient))
                    return std::string("Lottie gradient fill is malformed or unsupported");
                fill_paints.push_back(std::nullopt);
                fill_gradients.push_back(std::move(gradient));
            } else {
                LottieSolidPaint paint;
                if (!decode_lottie_solid_paint(*fill, paint, "lottie_fill_color", "lottie_fill_opacity"))
                    return std::string("Lottie fill must have bounded RGBA color/opacity values or keyframes");
                fill_paints.push_back(std::move(paint));
                fill_gradients.push_back(std::nullopt);
            }
        }
        std::vector<std::optional<StrokeStyle>> stroke_styles;
        std::vector<std::optional<AnimationTrackPlan>> stroke_color_tracks;
        std::vector<std::optional<AnimationTrackPlan>> stroke_opacity_tracks;
        std::vector<std::optional<AnimationTrackPlan>> stroke_width_tracks;
        for (const auto* stroke : strokes) {
            LottieSolidPaint paint;
            float width = 0.0f;
            std::optional<AnimationTrackPlan> width_animation;
            if (!decode_lottie_solid_paint(*stroke, paint, "lottie_stroke_color", "lottie_stroke_opacity"))
                return std::string("Lottie stroke color/opacity values or keyframes are unsupported");
            if (!lottie_static_scalar_property(*stroke, "w", width)) {
                AnimationTrackPlan track;
                if (!lottie_decode_transform_track(*stroke, "w", "lottie_stroke_width", 1.0f, track) ||
                    track.keyframes.empty() || std::any_of(track.keyframes.begin(), track.keyframes.end(),
                        [](const auto& key) { return key.value.size() != 1 || key.value[0] < 0.0f; }))
                    return std::string("Lottie stroke width must be bounded and static or animated");
                width = track.keyframes.front().value.front();
                width_animation = std::move(track);
            }
            if (width < 0.0f ||
                stroke->value("lc", 1) != 1 || stroke->value("lj", 1) != 1 ||
                stroke->value("ml", 4.0f) != 4.0f)
                return std::string("Lottie stroke requires bounded color/opacity/width values and default cap/join");
            StrokeStyle style;
            style.width = width;
            style.solid_color = paint.color;
            if (stroke->contains("d")) {
                if (!(*stroke)["d"].is_array() || (*stroke)["d"].size() > 128)
                    return std::string("Lottie stroke dash list is malformed or exceeds 128 entries");
                for (const auto& dash : (*stroke)["d"]) {
                    if (!dash.is_object() || !dash.contains("n") || !dash.contains("v"))
                        return std::string("Lottie stroke dash entry is malformed");
                    float value = 0.0f;
                    if (!lottie_static_scalar_property(dash, "v", value) || value < 0.0f)
                        return std::string("Lottie stroke dash values must be static and non-negative");
                    const auto kind = dash["n"].get<std::string>();
                    if (kind == "o") style.dash_offset = value;
                    else if (kind == "d" || kind == "g") style.dash_array.push_back(value);
                    else return std::string("unsupported Lottie stroke dash entry");
                }
                if (style.dash_array.empty())
                    return std::string("Lottie stroke dash list has no dash or gap entries");
            }
            stroke_styles.push_back(std::move(style));
            stroke_color_tracks.push_back(std::move(paint.color_animation));
            stroke_opacity_tracks.push_back(std::move(paint.opacity_animation));
            stroke_width_tracks.push_back(std::move(width_animation));
        }
        paints.fill_paints = std::move(fill_paints);
        paints.fill_gradients = std::move(fill_gradients);
        paints.stroke_styles = std::move(stroke_styles);
        paints.stroke_color_tracks = std::move(stroke_color_tracks);
        paints.stroke_opacity_tracks = std::move(stroke_opacity_tracks);
        paints.stroke_width_tracks = std::move(stroke_width_tracks);
    return std::nullopt;
}


} // namespace chronon3d::render_plan::detail
