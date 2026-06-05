#pragma once

#include <chronon3d/math/color.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::graph::detail {

/// Clamped pixel fetch — returns transparent for out-of-bounds coords.
inline Color get_clamped_pixel(const Color* src, int32_t src_w, int32_t src_h, int32_t stride,
                               int32_t px, int32_t py) {
    if (px < 0 || px >= src_w || py < 0 || py >= src_h) return Color::transparent();
    return src[py * stride + px];
}

/// Bilinear sample at (sx, sy) in source pixel space.
/// sx/sy are continuous source pixel coordinates.
inline Color sample_bilinear(const Color* src, int32_t src_w, int32_t src_h, int32_t stride,
                             float sx, float sy) {
    const float u = sx - 0.5f;
    const float v = sy - 0.5f;
    const int32_t x0 = static_cast<int32_t>(u >= 0.0f ? u : std::floor(u));
    const int32_t y0 = static_cast<int32_t>(v >= 0.0f ? v : std::floor(v));
    const int32_t x1 = x0 + 1;
    const int32_t y1 = y0 + 1;
    const float tx = u - static_cast<float>(x0);
    const float ty = v - static_cast<float>(y0);

    // Fast path: all 4 samples are guaranteed in-bounds (interior pixel).
    // The outer bounds check in execute_affine_rows / execute_projective_rows
    // already guarantees sx in [0, src_w) and sy in [0, src_h).  For interior
    // pixels (x1 < src_w && y1 < src_h) we skip the 8 bounds comparisons.
    // This covers the vast majority of pixels in a typical 1080p+ render.
    if (x1 < src_w && y1 < src_h && x0 >= 0 && y0 >= 0) {
        const Color* base = src + static_cast<size_t>(y0) * stride;
        const Color* base_next = src + static_cast<size_t>(y1) * stride;
        const Color& c00 = base[x0];
        const Color& c10 = base[x1];
        const Color& c01 = base_next[x0];
        const Color& c11 = base_next[x1];
        return lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
    }

    const Color c00 = get_clamped_pixel(src, src_w, src_h, stride, x0, y0);
    const Color c10 = get_clamped_pixel(src, src_w, src_h, stride, x1, y0);
    const Color c01 = get_clamped_pixel(src, src_w, src_h, stride, x0, y1);
    const Color c11 = get_clamped_pixel(src, src_w, src_h, stride, x1, y1);

    return lerp(lerp(c00, c10, tx), lerp(c01, c11, tx), ty);
}

/// Nearest-neighbour sample at (sx, sy).
inline Color sample_nearest(const Color* src, int32_t src_w, int32_t src_h, int32_t stride,
                            float sx, float sy) {
    const int32_t ix = static_cast<int32_t>(sx);
    const int32_t iy = static_cast<int32_t>(sy);
    return get_clamped_pixel(src, src_w, src_h, stride, ix, iy);
}

/// Multiply every channel (including alpha) by opacity.
inline Color apply_opacity(Color c, float opacity) {
    if (opacity >= 0.999f) return c;
    c.r *= opacity;
    c.g *= opacity;
    c.b *= opacity;
    c.a *= opacity;
    return c;
}

} // namespace chronon3d::graph::detail
