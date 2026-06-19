#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/compositor/alpha.hpp>
#include <cmath>

namespace chronon3d {

enum class BlendMode : uint8_t {
    Normal     = 0,
    Add        = 1,
    Multiply   = 2,
    Screen     = 3,
    Overlay    = 4,

    // Basic blend modes (Darken, Lighten, Difference, Exclusion)
    Darken     = 5,
    Lighten    = 6,
    Difference = 7,
    Exclusion  = 8,

    // Advanced blend modes (SoftLight, HardLight, ColorDodge, ColorBurn)
    SoftLight  = 9,
    HardLight  = 10,
    ColorDodge = 11,
    ColorBurn  = 12
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
        case BlendMode::Darken: {
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            return {std::min(src.r, dst.r), std::min(src.g, dst.g),
                    std::min(src.b, dst.b), a};
        }
        case BlendMode::Lighten: {
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            return {std::max(src.r, dst.r), std::max(src.g, dst.g),
                    std::max(src.b, dst.b), a};
        }
        case BlendMode::Difference: {
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            return {std::abs(src.r - dst.r), std::abs(src.g - dst.g),
                    std::abs(src.b - dst.b), a};
        }
        case BlendMode::Exclusion: {
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            return {src.r + dst.r - 2.0f * src.r * dst.r,
                    src.g + dst.g - 2.0f * src.g * dst.g,
                    src.b + dst.b - 2.0f * src.b * dst.b, a};
        }
        case BlendMode::SoftLight: {
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            auto sl = [](f32 cb, f32 cs) -> f32 {
                if (cs <= 0.5f) return cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb);
                auto d = [](f32 c) -> f32 {
                    if (c <= 0.25f) return ((16.0f * c - 12.0f) * c + 4.0f) * c;
                    return std::sqrt(c);
                };
                return cb + (2.0f * cs - 1.0f) * (d(cb) - cb);
            };
            // Clamp input straight RGB to [0,1] for the blend function (HDR contract).
            // Matches SIMD clamping in highway_color_kernels.cpp.
            return {sl(std::clamp(dst.r, 0.0f, 1.0f), std::clamp(src.r, 0.0f, 1.0f)),
                    sl(std::clamp(dst.g, 0.0f, 1.0f), std::clamp(src.g, 0.0f, 1.0f)),
                    sl(std::clamp(dst.b, 0.0f, 1.0f), std::clamp(src.b, 0.0f, 1.0f)), a};
        }
        case BlendMode::HardLight: {
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            auto hl = [](f32 cb, f32 cs) -> f32 {
                if (cs <= 0.5f) return 2.0f * cb * cs;
                return 1.0f - 2.0f * (1.0f - cb) * (1.0f - cs);
            };
            return {hl(std::clamp(dst.r, 0.0f, 1.0f), std::clamp(src.r, 0.0f, 1.0f)),
                    hl(std::clamp(dst.g, 0.0f, 1.0f), std::clamp(src.g, 0.0f, 1.0f)),
                    hl(std::clamp(dst.b, 0.0f, 1.0f), std::clamp(src.b, 0.0f, 1.0f)), a};
        }
        case BlendMode::ColorDodge: {
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            auto dodge = [](f32 cb, f32 cs) -> f32 {
                if (cs >= 1.0f) return 1.0f;
                if (cb <= 0.0f) return 0.0f;
                return std::min(1.0f, cb / std::max(1.0f - cs, 1e-8f));
            };
            return {dodge(std::clamp(dst.r, 0.0f, 1.0f), std::clamp(src.r, 0.0f, 1.0f)),
                    dodge(std::clamp(dst.g, 0.0f, 1.0f), std::clamp(src.g, 0.0f, 1.0f)),
                    dodge(std::clamp(dst.b, 0.0f, 1.0f), std::clamp(src.b, 0.0f, 1.0f)), a};
        }
        case BlendMode::ColorBurn: {
            const f32 a = src.a + dst.a * (1.0f - src.a);
            if (a <= 0.0f) return {0,0,0,0};
            auto burn = [](f32 cb, f32 cs) -> f32 {
                if (cs <= 0.0f) return 0.0f;
                if (cb >= 1.0f) return 1.0f;
                return 1.0f - std::min(1.0f, (1.0f - cb) / std::max(cs, 1e-8f));
            };
            return {burn(std::clamp(dst.r, 0.0f, 1.0f), std::clamp(src.r, 0.0f, 1.0f)),
                    burn(std::clamp(dst.g, 0.0f, 1.0f), std::clamp(src.g, 0.0f, 1.0f)),
                    burn(std::clamp(dst.b, 0.0f, 1.0f), std::clamp(src.b, 0.0f, 1.0f)), a};
        }
        default:
            return blend_normal(src, dst);
    }
}

} // namespace compositor

} // namespace chronon3d
