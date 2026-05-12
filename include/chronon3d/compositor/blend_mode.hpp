#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/compositor/alpha.hpp>

namespace chronon3d {

enum class BlendMode {
    Normal,
    Add,
    Multiply,
    Screen,
    Overlay
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
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            return {src.r * dst.r, src.g * dst.g, src.b * dst.b, a};
        }
        case BlendMode::Screen: {
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            return {
                1.0f - (1.0f - src.r) * (1.0f - dst.r),
                1.0f - (1.0f - src.g) * (1.0f - dst.g),
                1.0f - (1.0f - src.b) * (1.0f - dst.b),
                a
            };
        }
        case BlendMode::Overlay: {
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            auto ov = [](f32 s, f32 d) {
                return d < 0.5f ? 2.0f * s * d : 1.0f - 2.0f * (1.0f - s) * (1.0f - d);
            };
            return {ov(src.r, dst.r), ov(src.g, dst.g), ov(src.b, dst.b), a};
        }
        default:
            return blend_normal(src, dst);
    }
}

} // namespace compositor

} // namespace chronon3d
