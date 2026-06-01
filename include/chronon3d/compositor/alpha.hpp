#pragma once

#include <chronon3d/math/color.hpp>
#include <cmath>

namespace chronon3d {
namespace compositor {

/**
 * Standard alpha blending (Normal mode)
 * out.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
 * out.a   = src.a + dst.a * (1 - src.a)
 * 
 * This assumes non-premultiplied alpha.
 */
inline Color blend_normal(const Color& src, const Color& dst) {
    // Guard: NaN/Inf in either color would propagate and contaminate the framebuffer.
    if (std::isnan(src.r) || std::isnan(src.g) || std::isnan(src.b) || std::isnan(src.a) ||
        std::isinf(src.r) || std::isinf(src.g) || std::isinf(src.b) || std::isinf(src.a) ||
        std::isnan(dst.r) || std::isnan(dst.g) || std::isnan(dst.b) || std::isnan(dst.a) ||
        std::isinf(dst.r) || std::isinf(dst.g) || std::isinf(dst.b) || std::isinf(dst.a)) {
        return Color::transparent();
    }
    f32 out_a = src.a + dst.a * (1.0f - src.a);
    
    // If output alpha is zero, color doesn't matter much but we avoid NaN
    if (out_a <= 0.0f) return {0, 0, 0, 0};

    // Standard "over" composite with premultiplied destination.
    // Source is straight (non-premultiplied) alpha; we premultiply during blend.
    // Destination is expected to be premultiplied (as produced by this same operator).
    f32 r = src.r * src.a + dst.r * (1.0f - src.a);
    f32 g = src.g * src.a + dst.g * (1.0f - src.a);
    f32 b = src.b * src.a + dst.b * (1.0f - src.a);

    return {r, g, b, out_a};
}

} // namespace compositor
} // namespace chronon3d
