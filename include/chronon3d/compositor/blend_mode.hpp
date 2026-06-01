#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/compositor/alpha.hpp>
#include <cmath>

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
    if (src.a <= 0.0f) {
        return dst;
    }
    // Guard: if either color carries NaN or Inf, return transparent
    // to prevent contamination of the entire framebuffer.
    if (std::isnan(src.r) || std::isnan(src.g) || std::isnan(src.b) || std::isnan(src.a) ||
        std::isinf(src.r) || std::isinf(src.g) || std::isinf(src.b) || std::isinf(src.a) ||
        std::isnan(dst.r) || std::isnan(dst.g) || std::isnan(dst.b) || std::isnan(dst.a) ||
        std::isinf(dst.r) || std::isinf(dst.g) || std::isinf(dst.b) || std::isinf(dst.a)) {
        return Color::transparent();
    }
    switch (mode) {
        case BlendMode::Normal: {
            return blend_normal(src, dst);
        }
        case BlendMode::Add: {
            // Pure additive blend — does NOT clamp to 1.0.
            // HDR values (>1.0) are valid and expected for glow, bloom, etc.
            // Clamping is handled at the framebuffer write boundary where needed.
            return {
                src.r + dst.r,
                src.g + dst.g,
                src.b + dst.b,
                src.a + dst.a
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
