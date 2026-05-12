#pragma once

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace raster {

struct BBox {
    i32 x0, y0, x1, y1;
    
    void clip_to(i32 w, i32 h) {
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(w, x1);
        y1 = std::min(h, y1);
    }
    
    bool is_empty() const { return x0 >= x1 || y0 >= y1; }
};

// Point-in-shape tests (Local space, centered at 0,0)

inline bool point_in_rect(f32 px, f32 py, f32 hw, f32 hh) {
    return std::abs(px) <= hw && std::abs(py) <= hh;
}

inline bool point_in_rounded_rect(f32 px, f32 py, f32 hw, f32 hh, f32 r) {
    const f32 qx = std::max(std::abs(px) - (hw - r), 0.0f);
    const f32 qy = std::max(std::abs(py) - (hh - r), 0.0f);
    return qx * qx + qy * qy <= r * r;
}

inline bool point_in_circle(f32 px, f32 py, f32 r) {
    return px * px + py * py <= r * r;
}

// Basic blending (Normal mode)
inline Color blend_normal(const Color& src, const Color& dst) {
    if (src.a >= 1.0f) return src;
    if (src.a <= 0.0f) return dst;
    
    return Color{
        src.r * src.a + dst.r * (1.0f - src.a),
        src.g * src.a + dst.g * (1.0f - src.a),
        src.b * src.a + dst.b * (1.0f - src.a),
        src.a + dst.a * (1.0f - src.a)
    };
}

} // namespace raster
} // namespace chronon3d
