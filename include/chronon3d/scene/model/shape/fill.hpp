#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <vector>

namespace chronon3d {

enum class FillType {
    Solid,
    LinearGradient,
    RadialGradient,
    ConicGradient,
};

struct GradientStop {
    f32   offset{0.0f};   // normalised [0..1]
    Color color{1, 1, 1, 1};
};

struct GradientFill {
    FillType type{FillType::LinearGradient};
    std::vector<GradientStop> stops;
    Vec2 from{0.0f, 0.0f};  // in shape-local normalised coords [0..1]
    Vec2 to{1.0f, 0.0f};
};

struct Fill {
    bool        enabled{true};
    FillType    type{FillType::Solid};
    Color       solid{1, 1, 1, 1};
    GradientFill gradient;

    static Fill solid_color(Color c) {
        return Fill{true, FillType::Solid, c, {}};
    }

    static Fill linear(Vec2 from, Vec2 to, std::vector<GradientStop> stops) {
        Fill f;
        f.type = FillType::LinearGradient;
        f.gradient.type  = FillType::LinearGradient;
        f.gradient.from  = from;
        f.gradient.to    = to;
        f.gradient.stops = std::move(stops);
        return f;
    }

    static Fill radial(Vec2 center, f32 radius_norm, std::vector<GradientStop> stops) {
        Fill f;
        f.type = FillType::RadialGradient;
        f.gradient.type  = FillType::RadialGradient;
        f.gradient.from  = center;
        f.gradient.to    = {center.x + radius_norm, center.y};
        f.gradient.stops = std::move(stops);
        return f;
    }

    static Fill conic(Vec2 center, f32 angle_rad, std::vector<GradientStop> stops) {
        Fill f;
        f.type = FillType::ConicGradient;
        f.gradient.type  = FillType::ConicGradient;
        f.gradient.from  = center;
        f.gradient.to    = {center.x + std::cos(angle_rad), center.y + std::sin(angle_rad)};
        f.gradient.stops = std::move(stops);
        return f;
    }
};

// Sample gradient stops at normalised t [0..1] in linear space.
inline Color sample_gradient(const GradientFill& g, f32 t) {
    if (g.stops.empty()) return {1, 1, 1, 1};
    if (t <= g.stops.front().offset) return g.stops.front().color.to_linear();
    if (t >= g.stops.back().offset)  return g.stops.back().color.to_linear();

    for (std::size_t i = 1; i < g.stops.size(); ++i) {
        const auto& a = g.stops[i - 1];
        const auto& b = g.stops[i];
        if (t >= a.offset && t <= b.offset) {
            const f32 range = b.offset - a.offset;
            const f32 local = (range > 1e-6f) ? (t - a.offset) / range : 0.0f;
            Color c_a = a.color.to_linear();
            Color c_b = b.color.to_linear();
            return c_a + (c_b - c_a) * local;
        }
    }
    return g.stops.back().color.to_linear();
}

} // namespace chronon3d
