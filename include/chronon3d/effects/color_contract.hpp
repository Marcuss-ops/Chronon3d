#pragma once

// ── Color Contract ──────────────────────────────────────────────────────────
//
// Internal colour pipeline contract:
//   - Framebuffer stores premultiplied linear RGBA (HDR-capable)
//   - RGB premultiplied by alpha
//   - HDR allowed: r/g/b may be < 0 or > 1
//   - Alpha is normally in [0, 1]
//
// Effect colour processing should follow this pattern:
//   1. Unpremultiply (RGB /= alpha)   → StraightRgb
//   2. Apply linear RGB transform     (no clamp)
//   3. Premultiply (RGB *= alpha)     → Color
//
// Do NOT apply std::clamp(0,1) to the HDR-capable colour path.

#include <chronon3d/math/color.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::color {

/// Straight (non-premultiplied) RGB triplet.
/// Used as an intermediate representation between unpremultiply and
/// premultiply steps.
struct StraightRgb {
    float r{0.0f};
    float g{0.0f};
    float b{0.0f};
};

/// Unpremultiply the RGB channels of a premultiplied Color.
/// Returns {0,0,0} when alpha is (near) zero.
[[nodiscard]] inline StraightRgb unpremultiply_rgb(const Color& c) noexcept {
    constexpr float eps = 1.0e-8f;
    if (c.a <= eps) {
        return {0.0f, 0.0f, 0.0f};
    }
    const float inv_a = 1.0f / c.a;
    return {c.r * inv_a, c.g * inv_a, c.b * inv_a};
}

/// Premultiply straight RGB by alpha back into premultiplied Color.
/// Returns transparent when alpha is (near) zero.
[[nodiscard]] inline Color premultiply_rgb(StraightRgb rgb, float alpha) noexcept {
    constexpr float eps = 1.0e-8f;
    if (alpha <= eps) {
        return Color::transparent();
    }
    return {rgb.r * alpha, rgb.g * alpha, rgb.b * alpha, alpha};
}

} // namespace chronon3d::color
