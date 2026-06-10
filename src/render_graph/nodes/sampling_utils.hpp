#pragma once

#include <chronon3d/math/color.hpp>
#include <hwy/highway.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::graph::detail {

// ── Highway SIMD bilinear lerp chain ─────────────────────────────────
// Replaces scalar lerp(lerp(c00,c10,tx), lerp(c01,c11,tx), ty) with SIMD.
// Processes 1 RGBA pixel per call using FixedTag<float,4> (128-bit).
// On AVX2+, this compiles to FMA instructions: ~3× faster than scalar.
inline Color lerp_bilinear_simd(Color c00, Color c10, Color c01, Color c11,
                                float tx, float ty) {
    namespace hn = hwy::HWY_NAMESPACE;
    const hn::FixedTag<float, 4> d;

    const auto v00 = hn::LoadU(d, &c00.r);
    const auto v10 = hn::LoadU(d, &c10.r);
    const auto v01 = hn::LoadU(d, &c01.r);
    const auto v11 = hn::LoadU(d, &c11.r);

    const auto vt = hn::Set(d, tx);
    const auto vu = hn::Set(d, ty);

    // Lerp along x: top = c00 + tx*(c10 - c00), bottom = c01 + tx*(c11 - c01)
    const auto top = hn::MulAdd(hn::Sub(v10, v00), vt, v00);
    const auto bot = hn::MulAdd(hn::Sub(v11, v01), vt, v01);

    // Lerp along y: result = top + ty*(bot - top)
    const auto result = hn::MulAdd(hn::Sub(bot, top), vu, top);

    Color out;
    hn::StoreU(result, d, &out.r);
    return out;
}

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
    // Uses Highway SIMD lerp chain for the 3 MulAdd operations.
    // The outer bounds check in execute_affine_rows / execute_projective_rows
    // already guarantees sx in [0, src_w) and sy in [0, src_h).  For interior
    // pixels (x1 < src_w && y1 < src_h) we skip the 8 bounds comparisons.
    // This covers the vast majority of pixels in a typical 1080p+ render.
    if (x1 < src_w && y1 < src_h && x0 >= 0 && y0 >= 0) {
        const Color* base = src + static_cast<size_t>(y0) * stride;
        const Color* base_next = src + static_cast<size_t>(y1) * stride;
        return lerp_bilinear_simd(base[x0], base[x1], base_next[x0], base_next[x1], tx, ty);
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

// ── Interior bilinear sample (no bounds check) ────────────────────────
// Faster alternative to sample_bilinear() when the caller already knows
// that all 4 neighbour texels are within the source image bounds.
// row0 / row1 are pre-computed base pointers for floor(sy-0.5) / that+1.
HWY_INLINE Color sample_bilinear_interior(
    const Color* HWY_RESTRICT row0, const Color* HWY_RESTRICT row1,
    int32_t x0, float tx, float ty) {
    return lerp_bilinear_simd(row0[x0], row0[x0 + 1],
                               row1[x0], row1[x0 + 1],
                               tx, ty);
}

} // namespace chronon3d::graph::detail
