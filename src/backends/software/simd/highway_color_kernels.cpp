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
    // FixedTag<float,4> = 128-bit = 1 pixel (r,g,b,a).
    // Used for remainder on all targets, and as the sole path on
    // SSE4/NEON where ScalableTag has only 4 lanes.
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);

    int i = 0;

    // ── 2-pixel AVX2 path ─────────────────────────────────────────
    // ScalableTag (8 lanes on AVX2) → Half = 4 lanes = 1 pixel.
    // Restricted to AVX2 only: AVX-512 (16 lanes → Half = 8 lanes)
    // would cause Broadcast<3> to alias the wrong alpha for pixel 1.
#if HWY_TARGET == HWY_AVX2
    if (pixel_count >= 2) {
        const hn::ScalableTag<float> d;
        const hn::Half<decltype(d)> dh;          // 4 lanes → 1 pixel
        const auto ones = hn::Set(dh, 1.0f);      // same tag as half vectors

        for (; i + 1 < pixel_count; i += 2) {
            if ((i & 14) == 0 && (i + 16) < pixel_count) {
                __builtin_prefetch(&src_ptr[(i + 16) * 4], 0, 1);
                __builtin_prefetch(&dst_ptr[(i + 16) * 4], 1, 1);
            }
            // Early exit: skip fully-transparent pairs entirely
            if (src_ptr[i * 4 + 3] <= 0.0f && src_ptr[(i + 1) * 4 + 3] <= 0.0f) continue;
            const auto src_v = hn::LoadU(d, src_ptr + i * 4);  // 8 floats = 2 pixels
            const auto dst_v = hn::LoadU(d, dst_ptr + i * 4);

            // Split into per-pixel halves, broadcast alpha, blend, recombine
            const auto s_lo = hn::LowerHalf(dh, src_v);
            const auto s_hi = hn::UpperHalf(dh, src_v);
            const auto d_lo = hn::LowerHalf(dh, dst_v);
            const auto d_hi = hn::UpperHalf(dh, dst_v);

            const auto a_lo = hn::Broadcast<3>(s_lo);        // {a0,a0,a0,a0}
            const auto a_hi = hn::Broadcast<3>(s_hi);        // {a1,a1,a1,a1}
            const auto i_lo = hn::Sub(ones, a_lo);           // 1-a0
            const auto i_hi = hn::Sub(ones, a_hi);           // 1-a1

            const auto r_lo = hn::MulAdd(d_lo, i_lo, s_lo);  // src + dst*(1-a)
            const auto r_hi = hn::MulAdd(d_hi, i_hi, s_hi);

            const auto result = hn::Combine(d, r_hi, r_lo);
            hn::StoreU(result, d, dst_ptr + i * 4);
        }
    }
#endif

    // ── Remainder / SSE4/NEON fallback: 1 pixel × FixedTag ─────────
    for (; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            __builtin_prefetch(&src_ptr[(i + 16) * 4], 0, 1);
            __builtin_prefetch(&dst_ptr[(i + 16) * 4], 1, 1);
        }
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto src    = hn::LoadU(d4, src_ptr + i * 4);   // {r,g,b,a}
        const auto dst    = hn::LoadU(d4, dst_ptr + i * 4);
        const auto alpha  = hn::Broadcast<3>(src);            // {a,a,a,a}
        const auto inv_sa = hn::Sub(one, alpha);              // 1-a
        const auto result = hn::MulAdd(dst, inv_sa, src);     // src+dst*(1-a)
        hn::StoreU(result, d4, dst_ptr + i * 4);
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

HWY_ATTR void composite_add_premul_impl(float* HWY_RESTRICT dst,
                                         const float* HWY_RESTRICT src,
                                         int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            __builtin_prefetch(&src[(i + 16) * 4], 0, 1);
            __builtin_prefetch(&dst[(i + 16) * 4], 1, 1);
        }
        float* d = dst + i * 4;
        const float* s = src + i * 4;
        d[0] += s[0];
        d[1] += s[1];
        d[2] += s[2];
        d[3] += s[3];
    }
}

HWY_ATTR void composite_multiply_premul_impl(float* HWY_RESTRICT dst,
                                              const float* HWY_RESTRICT src,
                                              int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            __builtin_prefetch(&src[(i + 16) * 4], 0, 1);
            __builtin_prefetch(&dst[(i + 16) * 4], 1, 1);
        }
        float* d = dst + i * 4;
        const float* s = src + i * 4;
        const float new_a = s[3] + d[3] * (1.0f - s[3]);
        d[0] = s[0] * d[0];
        d[1] = s[1] * d[1];
        d[2] = s[2] * d[2];
        d[3] = new_a;
        if (new_a <= 0.0f) {
            d[0] = 0.0f; d[1] = 0.0f; d[2] = 0.0f;
        }
    }
}

HWY_ATTR void composite_screen_premul_impl(float* HWY_RESTRICT dst,
                                            const float* HWY_RESTRICT src,
                                            int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            __builtin_prefetch(&src[(i + 16) * 4], 0, 1);
            __builtin_prefetch(&dst[(i + 16) * 4], 1, 1);
        }
        float* d = dst + i * 4;
        const float* s = src + i * 4;
        const float new_a = s[3] + d[3] * (1.0f - s[3]);
        d[0] = s[0] + d[0] - s[0] * d[0];
        d[1] = s[1] + d[1] - s[1] * d[1];
        d[2] = s[2] + d[2] - s[2] * d[2];
        d[3] = new_a;
        if (new_a <= 0.0f) {
            d[0] = 0.0f; d[1] = 0.0f; d[2] = 0.0f;
        }
    }
}

HWY_ATTR void composite_overlay_premul_impl(float* HWY_RESTRICT dst,
                                             const float* HWY_RESTRICT src,
                                             int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            __builtin_prefetch(&src[(i + 16) * 4], 0, 1);
            __builtin_prefetch(&dst[(i + 16) * 4], 1, 1);
        }
        float* d = dst + i * 4;
        const float* s = src + i * 4;
        const float new_a = s[3] + d[3] * (1.0f - s[3]);
        for (int c = 0; c < 3; ++c) {
            const float sd = s[c] * d[c];
            d[c] = d[c] < 0.5f
                ? 2.0f * sd
                : 1.0f - 2.0f * (1.0f - s[c]) * (1.0f - d[c]);
        }
        d[3] = new_a;
        if (new_a <= 0.0f) {
            d[0] = 0.0f; d[1] = 0.0f; d[2] = 0.0f;
        }
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace chronon3d::simd
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace chronon3d::simd {

HWY_EXPORT(composite_normal_premul_impl);
HWY_EXPORT(composite_add_premul_impl);
HWY_EXPORT(composite_multiply_premul_impl);
HWY_EXPORT(composite_screen_premul_impl);
HWY_EXPORT(composite_overlay_premul_impl);
HWY_EXPORT(premultiply_alpha_rgba8_impl);

// ── Helpers ──────────────────────────────────────────────────────────────

/// Check a single Color for NaN/Inf contamination.
static bool has_bad_color(const Color& c) {
    return std::isnan(c.r) || std::isnan(c.g) || std::isnan(c.b) || std::isnan(c.a) ||
           std::isinf(c.r) || std::isinf(c.g) || std::isinf(c.b) || std::isinf(c.a);
}

/// Canary check on first + last pixel of both buffers.  Returns true if safe
/// for the fast SIMD path; falls back to a safe scalar loop when contaminated.
static bool check_nan_canary(const Color* dst, const Color* src, int pixel_count) {
    return !has_bad_color(src[0]) && !has_bad_color(dst[0]) &&
           !has_bad_color(src[pixel_count - 1]) && !has_bad_color(dst[pixel_count - 1]);
}

/// Safe scalar fallback for Normal blend (called when NaN/Inf detected).
static void composite_normal_premul_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        const Color& s = src[i];
        Color& d = dst[i];
        if (has_bad_color(s) || has_bad_color(d)) continue;
        if (s.a <= 0.0f) continue;
        const float inv_sa = 1.0f - s.a;
        d.r = s.r + d.r * inv_sa;
        d.g = s.g + d.g * inv_sa;
        d.b = s.b + d.b * inv_sa;
        d.a = s.a + d.a * inv_sa;
    }
}

void composite_normal_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_normal_premul_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_normal_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

// ── Non-normal blend modes (Add, Multiply, Screen, Overlay) ────────────

static void composite_add_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        d.r += s.r; d.g += s.g; d.b += s.b; d.a += s.a;
    }
}

static void composite_multiply_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r *= s.r; d.g *= s.g; d.b *= s.b; d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

static void composite_screen_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r = s.r + d.r - s.r * d.r;
        d.g = s.g + d.g - s.g * d.g;
        d.b = s.b + d.b - s.b * d.b;
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

static void composite_overlay_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        // Overlay: if dst.ch < 0.5 → 2 * src.ch * dst.ch else 1 - 2*(1-src.ch)*(1-dst.ch)
        const float sr = s.r, sg = s.g, sb = s.b;
        const float dr = d.r, dg = d.g, db = d.b;
        d.r = dr < 0.5f ? 2.0f * sr * dr : 1.0f - 2.0f * (1.0f - sr) * (1.0f - dr);
        d.g = dg < 0.5f ? 2.0f * sg * dg : 1.0f - 2.0f * (1.0f - sg) * (1.0f - dg);
        d.b = db < 0.5f ? 2.0f * sb * db : 1.0f - 2.0f * (1.0f - sb) * (1.0f - db);
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

void composite_add_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_add_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_add_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void composite_multiply_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_multiply_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_multiply_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void composite_screen_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_screen_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_screen_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void composite_overlay_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_overlay_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_overlay_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void premultiply_alpha_rgba8(uint32_t* __restrict__ dst, const uint8_t* __restrict__ src, int pixel_count) {
    HWY_DYNAMIC_DISPATCH(premultiply_alpha_rgba8_impl)(dst, src, pixel_count);
}

void clear_framebuffer(Color* data, int pixel_count, const Color& color) {
    if (pixel_count <= 0) return;
    // Zero-fill via memset is ~4-8× faster than std::fill (which writes
    // one Color at a time) because the CPU's write-combining and ERMSB
    // rep stosb microcode handles large memset in ~1 cycle per 16 bytes.
    if (color.r == 0.0f && color.g == 0.0f && color.b == 0.0f && color.a == 0.0f) {
        std::memset(static_cast<void*>(data), 0,
                     static_cast<size_t>(pixel_count) * sizeof(Color));
        return;
    }
    std::fill(data, data + pixel_count, color);
}

}  // namespace chronon3d::simd
#endif
