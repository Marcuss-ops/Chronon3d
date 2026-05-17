#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/vec2.hpp>
#include <vector>

namespace chronon3d {

enum class FillType {
    Solid,
    LinearGradient,
    RadialGradient,
};

struct GradientStop {
    f32   offset{0.0f};   // normalised [0..1]
    Color color{1, 1, 1, 1};
};

struct GradientFill {
    std::vector<GradientStop> stops;
    Vec2 from{0.0f, 0.0f};  // in shape-local normalised coords [0..1]
    Vec2 to{1.0f, 0.0f};
};

struct Fill {
    FillType    type{FillType::Solid};
    Color       solid{1, 1, 1, 1};
    GradientFill gradient;

    static Fill solid_color(Color c) {
        return Fill{FillType::Solid, c, {}};
    }

    static Fill linear(Vec2 from, Vec2 to, std::vector<GradientStop> stops) {
        Fill f;
        f.type = FillType::LinearGradient;
        f.gradient.from  = from;
        f.gradient.to    = to;
        f.gradient.stops = std::move(stops);
        return f;
    }

    static Fill radial(Vec2 center, f32 radius_norm, std::vector<GradientStop> stops) {
        Fill f;
        f.type = FillType::RadialGradient;
        f.gradient.from  = center;
        f.gradient.to    = {center.x + radius_norm, center.y};
        f.gradient.stops = std::move(stops);
        return f;
    }
};

// Sample gradient stops at normalised t [0..1].
inline Color sample_gradient(const GradientFill& g, f32 t) {
    if (g.stops.empty()) return {1, 1, 1, 1};
    if (t <= g.stops.front().offset) return g.stops.front().color;
    if (t >= g.stops.back().offset)  return g.stops.back().color;

    for (std::size_t i = 1; i < g.stops.size(); ++i) {
        const auto& a = g.stops[i - 1];
        const auto& b = g.stops[i];
        if (t >= a.offset && t <= b.offset) {
            const f32 range = b.offset - a.offset;
            const f32 local = (range > 1e-6f) ? (t - a.offset) / range : 0.0f;
            return a.color + (b.color - a.color) * local;
        }
    }
    return g.stops.back().color;
}

} // namespace chronon3d
