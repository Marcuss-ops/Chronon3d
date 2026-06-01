#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_color_kernels.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include <chronon3d/simd/kernels.hpp>
#include <algorithm>
#include <cmath>

// Prefetch: use compiler builtins which work across all SIMD contexts.

HWY_BEFORE_NAMESPACE();
namespace chronon3d::simd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

HWY_ATTR void composite_normal_premul_impl(float* HWY_RESTRICT dst_ptr,
                                           const float* HWY_RESTRICT src_ptr,
                                           int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            __builtin_prefetch(&src_ptr[(i + 16) * 4], 0, 1);
            __builtin_prefetch(&dst_ptr[(i + 16) * 4], 1, 1);
        }
        float* d = dst_ptr + i * 4;
        const float* s = src_ptr + i * 4;
        float inv_sa = 1.0f - s[3];
        d[0] = s[0] + d[0] * inv_sa;
        d[1] = s[1] + d[1] * inv_sa;
        d[2] = s[2] + d[2] * inv_sa;
        d[3] = s[3] + d[3] * inv_sa;
    }
}

HWY_ATTR void premultiply_alpha_rgba8_impl(uint32_t* HWY_RESTRICT dst,
                                           const uint8_t* HWY_RESTRICT src,
                                           int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        const uint8_t r = src[i * 4 + 0];
        const uint8_t g = src[i * 4 + 1];
        const uint8_t b = src[i * 4 + 2];
        const uint8_t a = src[i * 4 + 3];
        const uint32_t pr = (static_cast<uint32_t>(r) * a + 127) / 255;
        const uint32_t pg = (static_cast<uint32_t>(g) * a + 127) / 255;
        const uint32_t pb = (static_cast<uint32_t>(b) * a + 127) / 255;
        dst[i] = (static_cast<uint32_t>(a) << 24) | (pr << 16) | (pg << 8) | pb;
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace chronon3d::simd
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace chronon3d::simd {

HWY_EXPORT(composite_normal_premul_impl);
HWY_EXPORT(premultiply_alpha_rgba8_impl);

void composite_normal_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;

    // Canary check: scan first + last pixel for NaN/Inf.
    // NaN/Inf in the framebuffer indicates corrupted data — fall back to a
    // scalar safe loop that skips bad pixels instead of propagating them.
    auto has_bad = [](const Color& c) {
        return std::isnan(c.r) || std::isnan(c.g) || std::isnan(c.b) || std::isnan(c.a) ||
               std::isinf(c.r) || std::isinf(c.g) || std::isinf(c.b) || std::isinf(c.a);
    };

    if (has_bad(src[0]) || has_bad(dst[0]) ||
        has_bad(src[pixel_count - 1]) || has_bad(dst[pixel_count - 1])) {
        // Safe scalar fallback: skip NaN/Inf pixels to prevent contamination.
        for (int i = 0; i < pixel_count; ++i) {
            const Color& s = src[i];
            Color& d = dst[i];
            if (has_bad(s) || has_bad(d)) continue;
            if (s.a <= 0.0f) continue;
            const float inv_sa = 1.0f - s.a;
            d.r = s.r + d.r * inv_sa;
            d.g = s.g + d.g * inv_sa;
            d.b = s.b + d.b * inv_sa;
            d.a = s.a + d.a * inv_sa;
        }
        return;
    }

    HWY_DYNAMIC_DISPATCH(composite_normal_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void premultiply_alpha_rgba8(uint32_t* __restrict__ dst, const uint8_t* __restrict__ src, int pixel_count) {
    HWY_DYNAMIC_DISPATCH(premultiply_alpha_rgba8_impl)(dst, src, pixel_count);
}

void clear_framebuffer(Color* data, int pixel_count, const Color& color) {
    std::fill(data, data + pixel_count, color);
}

}  // namespace chronon3d::simd
#endif
