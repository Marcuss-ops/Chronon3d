#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/compositor/alpha.hpp>

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
            return blend_normal(src, dst);
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
