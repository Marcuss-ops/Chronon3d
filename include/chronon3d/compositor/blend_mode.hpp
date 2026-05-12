#pragma once

#include <chronon3d/math/color.hpp>

namespace chronon3d {

enum class BlendMode {
    Normal,
    Add,
    Multiply
};

namespace compositor {

inline Color blend(const Color& src, const Color& dst, BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: {
            // Standard alpha blending: src.a * src + (1 - src.a) * dst
            f32 out_a = src.a + dst.a * (1.0f - src.a);
            if (out_a <= 0.0f) return {0,0,0,0};
            
            f32 r = (src.r * src.a + dst.r * dst.a * (1.0f - src.a)) / out_a;
            f32 g = (src.g * src.a + dst.g * dst.a * (1.0f - src.a)) / out_a;
            f32 b = (src.b * src.a + dst.b * dst.a * (1.0f - src.a)) / out_a;
            
            return {r, g, b, out_a};
        }
        case BlendMode::Add: {
            return {
                std::min(src.r + dst.r, 1.0f),
                std::min(src.g + dst.g, 1.0f),
                std::min(src.b + dst.b, 1.0f),
                std::min(src.a + dst.a, 1.0f)
            };
        }
        case BlendMode::Multiply: {
            return {
                src.r * dst.r,
                src.g * dst.g,
                src.b * dst.b,
                src.a * dst.a
            };
        }
        default:
            return src;
    }
}

} // namespace compositor

} // namespace chronon3d
