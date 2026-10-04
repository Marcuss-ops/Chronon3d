#pragma once

// Gradient-aware scene stroke style. The graphics gradient definition is
// copied intact into scene payloads; there is no scene-specific gradient DTO.

#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/graphics/stroke/brush_profile.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/shape/path.hpp>

#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chronon3d::graphics {

struct StrokeStyle {
    bool enabled{false};
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    f32 width{1.0f};
    StrokeAlignment alignment{StrokeAlignment::Center};

    LineCap cap{LineCap::Butt};
    LineJoin join{LineJoin::Miter};
    std::vector<f32> dash_array{};
    f32 dash_offset{0.0f};
    f32 trim_start{0.0f};
    f32 trim_end{1.0f};
    f32 taper_start{1.0f};
    f32 taper_end{1.0f};
    std::optional<GradientDefinition> gradient;
    std::optional<BrushProfile> brush;

    static StrokeStyle solid(Color value, f32 stroke_width = 1.0f) {
        StrokeStyle result;
        result.enabled = true;
        result.color = value;
        result.width = stroke_width;
        return result;
    }

    static StrokeStyle linear_gradient(
        Vec2 from, Vec2 to, std::vector<GradientStop> stops,
        f32 stroke_width = 1.0f,
        GradientSpread spread = GradientSpread::Pad) {
        StrokeStyle result;
        result.enabled = true;
        result.width = stroke_width;
        result.gradient = GradientDefinition::linear(
            from, to, std::move(stops), spread);
        return result;
    }

    static StrokeStyle radial_gradient(
        Vec2 center, f32 radius, std::vector<GradientStop> stops,
        f32 stroke_width = 1.0f,
        GradientSpread spread = GradientSpread::Pad) {
        StrokeStyle result;
        result.enabled = true;
        result.width = stroke_width;
        result.gradient = GradientDefinition::radial(
            center, radius, std::move(stops), spread);
        return result;
    }

    static StrokeStyle conic_gradient(
        Vec2 center, f32 angle_rad, std::vector<GradientStop> stops,
        f32 stroke_width = 1.0f,
        GradientSpread spread = GradientSpread::Pad) {
        StrokeStyle result;
        result.enabled = true;
        result.width = stroke_width;
        result.gradient = GradientDefinition::conic(
            center, angle_rad, std::move(stops), spread);
        return result;
    }

    [[nodiscard]] bool is_solid() const noexcept {
        return !gradient.has_value() && !brush.has_value();
    }
    [[nodiscard]] bool is_gradient() const noexcept { return gradient.has_value(); }

    [[nodiscard]] ShapeStroke to_shape_stroke() const;
    [[nodiscard]] PathStroke to_path_stroke() const;
};

inline PathStroke StrokeStyle::to_path_stroke() const {
    if (brush && (!brush->valid() || !std::isfinite(width) || width < 0.0f ||
        (brush->size == 0.0f && width <= 0.0f)))
        throw std::invalid_argument("path brush stroke profile or base width is invalid");
    if (!std::isfinite(taper_start) || taper_start < 0.0f || taper_start > 1.0f ||
        !std::isfinite(taper_end) || taper_end < 0.0f || taper_end > 1.0f)
        throw std::invalid_argument("path stroke taper must be finite and in [0, 1]");
    if (brush && gradient)
        throw std::invalid_argument("brush strokes use the brush along-path gradient, not a stroke gradient");
    if (brush && (taper_start != 1.0f || taper_end != 1.0f))
        throw std::invalid_argument("brush taper must be authored in the brush profile");
    PathStroke result;
    result.enabled = enabled;
    result.color = color;
    result.width = width;
    result.cap = cap;
    result.join = join;
    result.dash_array = dash_array;
    result.dash_offset = dash_offset;
    result.trim_start = trim_start;
    result.trim_end = trim_end;
    result.taper_start = taper_start;
    result.taper_end = taper_end;
    result.gradient = gradient;
    result.brush = brush;
    return result;
}

inline ShapeStroke StrokeStyle::to_shape_stroke() const {
    if (brush)
        throw std::invalid_argument("brush strokes require a path-backed shape");
    ShapeStroke result;
    result.enabled = enabled;
    result.color = color;
    result.width = width;
    result.alignment = alignment;
    result.gradient = gradient;
    return result;
}

} // namespace chronon3d::graphics
