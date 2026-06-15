#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_color_kernels.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include <chronon3d/core/memory_utils.hpp>

#include <chronon3d/simd/kernels.hpp>
#include <chronon3d/compositor/blend_math.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

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
                chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
                chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
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
            chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
            chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
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

// ── PRGB32 ↔ Color row conversion ─────────────────────────────────────────
// Convert a row of premultiplied 0xAARRGGBB uint32 pixels to un-premultiplied
// float RGBA Color.
//
// Architecture: scalar uint32 unpack (cheap integer shifts) + SIMD float
// un-premultiply + SIMD clamp.  The 2-pixel AVX2 path processes 2 pixels per
// iteration with LowerHalf/UpperHalf per-pixel math, matching the
// composite_normal_premul_impl pattern.

HWY_ATTR void bl_image_prgb32_to_color_row_impl(float* HWY_RESTRICT dst_ptr,
                                                  const uint32_t* HWY_RESTRICT src,
                                                  int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto zero = hn::Zero(d4);
    const auto one  = hn::Set(d4, 1.0f);
    int i = 0;

    // ── 2-pixel AVX2 path ─────────────────────────────────────────────
    // Pattern: scalar unpack uint32 → float[4] per pixel, then SIMD float
    // un-premultiply + clamp on 2 pixels via LowerHalf/UpperHalf.
    // Restricted to AVX2: ScalableTag = 8 lanes, Half = 4 lanes = 1 pixel.
#if HWY_TARGET == HWY_AVX2
    if (pixel_count >= 2) {
        const hn::ScalableTag<float> d;              // 8 lanes on AVX2
        const hn::Half<decltype(d)> dh;              // 4 lanes → 1 pixel
        const auto scales = hn::Set(dh, 1.0f / 255.0f);
        const auto eps    = hn::Set(dh, 1e-8f);
        const auto zeros  = hn::Zero(dh);
        const auto ones   = hn::Set(dh, 1.0f);

        for (; i + 1 < pixel_count; i += 2) {
            if ((i & 14) == 0 && (i + 16) < pixel_count) {
                chronon3d::prefetch(&src[(i + 16)], false, 1);
                chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
            }

            const uint32_t p0 = src[i];
            const uint32_t p1 = src[i + 1];

            // Fast-path: skip fully transparent pairs entirely
            if ((p0 & 0xFF000000u) == 0 && (p1 & 0xFF000000u) == 0) {
                hn::StoreU(hn::Zero(d), d, dst_ptr + i * 4);  // 8 floats = 0
                continue;
            }

            // ── Scalar unpack PRGB32 → float (cheap integer shifts) ──
            float tmp0[4] = {
                static_cast<float>((p0 >> 16) & 0xFF),
                static_cast<float>((p0 >>  8) & 0xFF),
                static_cast<float>( p0        & 0xFF),
                static_cast<float>((p0 >> 24) & 0xFF)
            };
            float tmp1[4] = {
                static_cast<float>((p1 >> 16) & 0xFF),
                static_cast<float>((p1 >>  8) & 0xFF),
                static_cast<float>( p1        & 0xFF),
                static_cast<float>((p1 >> 24) & 0xFF)
            };

            // Load both pixels into per-pixel halves and scale to [0,1]
            auto lo = hn::Mul(hn::LoadU(dh, tmp0), scales);  // {r0,g0,b0,a0}
            auto hi = hn::Mul(hn::LoadU(dh, tmp1), scales);  // {r1,g1,b1,a1}

            // ── SIMD un-premultiply (same pattern as composite_normal_premul) ──
            const auto a_lo = hn::Broadcast<3>(lo);            // {a0,a0,a0,a0}
            const auto a_hi = hn::Broadcast<3>(hi);            // {a1,a1,a1,a1}

            // Safe division: where alpha ≤ epsilon → result is 0
            const auto safe_lo = hn::Max(a_lo, eps);
            const auto safe_hi = hn::Max(a_hi, eps);
            auto inv_lo = hn::Div(ones, safe_lo);               // 1/max(a,ε)
            auto inv_hi = hn::Div(ones, safe_hi);

            // Zero out fully transparent pixels BEFORE the multiply
            const auto mask_lo = hn::Gt(a_lo, eps);
            const auto mask_hi = hn::Gt(a_hi, eps);
            inv_lo = hn::IfThenElse(mask_lo, inv_lo, zeros);
            inv_hi = hn::IfThenElse(mask_hi, inv_hi, zeros);

            // result = raw_rgba * {1/a, 1/a, 1/a, 1/a}
            // For RGB: rgb/a = un-premultiplied.  For A: a/a = 1.0.
            auto r_lo = hn::Mul(lo, inv_lo);                    // {r0/a0, g0/a0, b0/a0, 1}
            auto r_hi = hn::Mul(hi, inv_hi);                    // {r1/a1, g1/a1, b1/a1, 1}

            // Fix alpha lane: overwrite with original alpha (not a/a = 1.0)
            float out0[4];
            float out1[4];
            hn::StoreU(r_lo, dh, out0);
            hn::StoreU(r_hi, dh, out1);
            out0[3] = hn::GetLane(a_lo);
            out1[3] = hn::GetLane(a_hi);
            if (out0[3] <= 1e-8f) { out0[0] = out0[1] = out0[2] = out0[3] = 0.0f; }
            if (out1[3] <= 1e-8f) { out1[0] = out1[1] = out1[2] = out1[3] = 0.0f; }

            // Store directly (math guarantees values in range; no extra clamp needed)
            hn::StoreU(hn::LoadU(dh, out0), dh, dst_ptr + i * 4);
            hn::StoreU(hn::LoadU(dh, out1), dh, dst_ptr + (i + 1) * 4);
        }
    }
#endif

    // ── 1-pixel remainder / SSE4/NEON fallback ───────────────────────
    for (; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            chronon3d::prefetch(&src[(i + 16)], false, 1);
            chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
        }
        const uint32_t p = src[i];
        if ((p & 0xFF000000u) == 0) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
            dst_ptr[i * 4 + 3] = 0.0f;
            continue;
        }
        const float a = static_cast<float>((p >> 24) & 0xFF) * (1.0f / 255.0f);
        const float inv_a = 1.0f / a;
        float tmp[4];
        tmp[0] = static_cast<float>((p >> 16) & 0xFF) * (1.0f / 255.0f) * inv_a;
        tmp[1] = static_cast<float>((p >>  8) & 0xFF) * (1.0f / 255.0f) * inv_a;
        tmp[2] = static_cast<float>( p        & 0xFF) * (1.0f / 255.0f) * inv_a;
        tmp[3] = a;
        auto v = hn::LoadU(d4, tmp);
        hn::StoreU(hn::Min(hn::Max(v, zero), one), d4, dst_ptr + i * 4);
    }
}

// Convert a row of float RGBA Color to premultiplied 0xAARRGGBB PRGB32.
//
// Architecture: SIMD float premultiply + clamp, scalar pack to PRGB32.
// The 2-pixel AVX2 path processes 2 pixels per iteration with
// LowerHalf/UpperHalf per-pixel premultiply, matching the
// composite_normal_premul_impl pattern.

HWY_ATTR void color_to_prgb32_row_impl(uint32_t* HWY_RESTRICT dst,
                                         const float* HWY_RESTRICT src_ptr,
                                         int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto zero = hn::Zero(d4);
    const auto one  = hn::Set(d4, 1.0f);
    int i = 0;

    // ── 2-pixel AVX2 path ─────────────────────────────────────────────
#if HWY_TARGET == HWY_AVX2
    if (pixel_count >= 2) {
        const hn::ScalableTag<float> d;              // 8 lanes on AVX2
        const hn::Half<decltype(d)> dh;              // 4 lanes → 1 pixel

        for (; i + 1 < pixel_count; i += 2) {
            if ((i & 14) == 0 && (i + 16) < pixel_count) {
                chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
                chronon3d::prefetch(&dst[(i + 16)], true, 1);
            }

            // Load 8 floats = 2 pixels of straight-alpha Color
            const auto rgba = hn::LoadU(d, src_ptr + i * 4);

            // Split into per-pixel halves
            const auto s_lo = hn::LowerHalf(dh, rgba);         // {r0,g0,b0,a0}
            const auto s_hi = hn::UpperHalf(dh, rgba);         // {r1,g1,b1,a1}

            // ── SIMD premultiply ──
            const auto a_lo = hn::Broadcast<3>(s_lo);           // {a0,a0,a0,a0}
            const auto a_hi = hn::Broadcast<3>(s_hi);           // {a1,a1,a1,a1}
            const auto ca_lo = hn::Min(hn::Max(a_lo, zero), one);
            const auto ca_hi = hn::Min(hn::Max(a_hi, zero), one);

            auto premul_lo = hn::Mul(s_lo, ca_lo);              // {r0*a0, g0*a0, b0*a0, a0*a0}
            auto premul_hi = hn::Mul(s_hi, ca_hi);
            premul_lo = hn::Min(hn::Max(premul_lo, zero), one);
            premul_hi = hn::Min(hn::Max(premul_hi, zero), one);

            // ── Scalar pack to PRGB32 (inherently scalar) ──
            float tmp0[4];
            float tmp1[4];
            hn::StoreU(premul_lo, dh, tmp0);
            hn::StoreU(premul_hi, dh, tmp1);
            const float pa0 = hn::GetLane(ca_lo);
            const float pa1 = hn::GetLane(ca_hi);

            dst[i]     = (static_cast<uint32_t>(pa0 * 255.0f + 0.5f) << 24) |
                         (static_cast<uint32_t>(tmp0[0] * 255.0f + 0.5f) << 16) |
                         (static_cast<uint32_t>(tmp0[1] * 255.0f + 0.5f) <<  8) |
                          static_cast<uint32_t>(tmp0[2] * 255.0f + 0.5f);
            dst[i + 1] = (static_cast<uint32_t>(pa1 * 255.0f + 0.5f) << 24) |
                         (static_cast<uint32_t>(tmp1[0] * 255.0f + 0.5f) << 16) |
                         (static_cast<uint32_t>(tmp1[1] * 255.0f + 0.5f) <<  8) |
                          static_cast<uint32_t>(tmp1[2] * 255.0f + 0.5f);
        }
    }
#endif

    // ── 1-pixel remainder / SSE4/NEON fallback ───────────────────────
    for (; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
            chronon3d::prefetch(&dst[(i + 16)], true, 1);
        }
        auto v = hn::LoadU(d4, src_ptr + i * 4);
        const auto alpha = hn::Broadcast<3>(v);
        const auto clamped_alpha = hn::Min(hn::Max(alpha, zero), one);
        auto premul = hn::Mul(v, clamped_alpha);
        premul = hn::Min(hn::Max(premul, zero), one);
        float tmp[4];
        hn::StoreU(premul, d4, tmp);
        const float pa = hn::GetLane(clamped_alpha);
        dst[i] = (static_cast<uint32_t>(pa * 255.0f + 0.5f) << 24) |
                 (static_cast<uint32_t>(tmp[0] * 255.0f + 0.5f) << 16) |
                 (static_cast<uint32_t>(tmp[1] * 255.0f + 0.5f) <<  8) |
                  static_cast<uint32_t>(tmp[2] * 255.0f + 0.5f);
    }
}

HWY_ATTR void composite_add_premul_impl(float* HWY_RESTRICT dst_ptr,
                                         const float* HWY_RESTRICT src_ptr,
                                         int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    int i = 0;

    // ── 2-pixel AVX2 path ─────────────────────────────────────────
#if HWY_TARGET == HWY_AVX2
    if (pixel_count >= 2) {
        const hn::ScalableTag<float> d;
        for (; i + 1 < pixel_count; i += 2) {
            if ((i & 14) == 0 && (i + 16) < pixel_count) {
                chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
                chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
            }
            if (src_ptr[i * 4 + 3] <= 0.0f && src_ptr[(i + 1) * 4 + 3] <= 0.0f) continue;
            const auto src_v = hn::LoadU(d, src_ptr + i * 4);
            const auto dst_v = hn::LoadU(d, dst_ptr + i * 4);
            const auto result = hn::Add(dst_v, src_v);
            hn::StoreU(result, d, dst_ptr + i * 4);
        }
    }
#endif

    // ── 1-pixel remainder ─────────────────────────────────────────
    for (; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
            chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
        }
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);
        const auto result = hn::Add(d, s);
        hn::StoreU(result, d4, dst_ptr + i * 4);
    }
}

HWY_ATTR void composite_multiply_premul_impl(float* HWY_RESTRICT dst_ptr,
                                              const float* HWY_RESTRICT src_ptr,
                                              int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    int i = 0;

    // ── 2-pixel AVX2 path ─────────────────────────────────────────
#if HWY_TARGET == HWY_AVX2
    if (pixel_count >= 2) {
        const hn::ScalableTag<float> d;
        for (; i + 1 < pixel_count; i += 2) {
            if ((i & 14) == 0 && (i + 16) < pixel_count) {
                chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
                chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
            }
            if (src_ptr[i * 4 + 3] <= 0.0f && src_ptr[(i + 1) * 4 + 3] <= 0.0f) continue;
            // Save original alpha BEFORE the Mul store overwrites it.
            const float da0 = dst_ptr[i * 4 + 3];
            const float da1 = dst_ptr[(i + 1) * 4 + 3];
            const auto src_v = hn::LoadU(d, src_ptr + i * 4);
            const auto dst_v = hn::LoadU(d, dst_ptr + i * 4);
            auto result = hn::Mul(src_v, dst_v);
            hn::StoreU(result, d, dst_ptr + i * 4);
            // Fix alpha for both pixels using saved original da.
            for (int p = 0; p < 2; ++p) {
                const float sa = src_ptr[(i + p) * 4 + 3];
                const float da = (p == 0) ? da0 : da1;
                float new_a = sa + da * (1.0f - sa);
                dst_ptr[(i + p) * 4 + 3] = new_a;
                if (new_a <= 0.0f) {
                    dst_ptr[(i + p) * 4 + 0] = 0.0f;
                    dst_ptr[(i + p) * 4 + 1] = 0.0f;
                    dst_ptr[(i + p) * 4 + 2] = 0.0f;
                }
            }
        }
    }
#endif

    // ── 1-pixel remainder ─────────────────────────────────────────
    for (; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
            chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
        }
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        // Save original alpha BEFORE the Mul store overwrites it.
        const float da_orig = dst_ptr[i * 4 + 3];
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);
        auto result = hn::Mul(s, d);
        hn::StoreU(result, d4, dst_ptr + i * 4);
        const float sa = src_ptr[i * 4 + 3];
        float new_a = sa + da_orig * (1.0f - sa);
        dst_ptr[i * 4 + 3] = new_a;
        if (new_a <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

HWY_ATTR void composite_screen_premul_impl(float* HWY_RESTRICT dst_ptr,
                                            const float* HWY_RESTRICT src_ptr,
                                            int pixel_count) {
    // Screen: result = src + dst - src*dst  (all 4 channels).
    // Alpha comes out correct: s.a + d.a - s.a*d.a = s.a + d.a*(1-s.a).
    const hn::FixedTag<float, 4> d4;
    int i = 0;

    // ── 2-pixel AVX2 path ─────────────────────────────────────────
#if HWY_TARGET == HWY_AVX2
    if (pixel_count >= 2) {
        const hn::ScalableTag<float> d;
        for (; i + 1 < pixel_count; i += 2) {
            if ((i & 14) == 0 && (i + 16) < pixel_count) {
                chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
                chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
            }
            if (src_ptr[i * 4 + 3] <= 0.0f && src_ptr[(i + 1) * 4 + 3] <= 0.0f) continue;
            const auto src_v = hn::LoadU(d, src_ptr + i * 4);
            const auto dst_v = hn::LoadU(d, dst_ptr + i * 4);
            const auto sum = hn::Add(src_v, dst_v);
            const auto prod = hn::Mul(src_v, dst_v);
            const auto result = hn::Sub(sum, prod);
            hn::StoreU(result, d, dst_ptr + i * 4);
        }
    }
#endif

    // ── 1-pixel remainder ─────────────────────────────────────────
    for (; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
            chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
        }
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);
        const auto sum = hn::Add(s, d);
        const auto prod = hn::Mul(s, d);
        auto result = hn::Sub(sum, prod);
        // Alpha from Screen formula = s.a + d.a*(1-s.a) — correct for premul over.
        // But check new_a ≤ 0 → zero out RGB.
        hn::StoreU(result, d4, dst_ptr + i * 4);
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

HWY_ATTR void composite_overlay_premul_impl(float* HWY_RESTRICT dst_ptr,
                                             const float* HWY_RESTRICT src_ptr,
                                             int pixel_count) {
    // Overlay: if dst < 0.5 → 2*src*dst, else → 1 - 2*(1-src)*(1-dst).
    // Alpha uses normal blend: new_a = s.a + d.a*(1-s.a).
    // Per-channel conditional → 1-pixel FixedTag Highway path.
    const hn::FixedTag<float, 4> d4;
    const auto half  = hn::Set(d4, 0.5f);
    const auto one   = hn::Set(d4, 1.0f);
    const auto two   = hn::Set(d4, 2.0f);
    const auto zero  = hn::Zero(d4);

    for (int i = 0; i < pixel_count; ++i) {
        if ((i & 15) == 0 && (i + 16) < pixel_count) {
            chronon3d::prefetch(&src_ptr[(i + 16) * 4], false, 1);
            chronon3d::prefetch(&dst_ptr[(i + 16) * 4], true, 1);
        }
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        // Branch 1 (dst < 0.5): 2 * src * dst
        const auto branch1 = hn::Mul(hn::Mul(s, d), two);

        // Branch 2 (dst ≥ 0.5): 1 - 2*(1-src)*(1-dst)
        const auto om_s = hn::Sub(one, s);
        const auto om_d = hn::Sub(one, d);
        const auto branch2 = hn::Sub(one, hn::Mul(hn::Mul(om_s, om_d), two));

        // Select per channel
        const auto mask = hn::Lt(d, half);  // true where dst < 0.5
        auto rgb_result = hn::IfThenElse(mask, branch1, branch2);

        // Alpha: normal blend = s.a + d.a*(1-s.a)
        const auto a_s = hn::Broadcast<3>(s);
        const auto a_d = hn::Broadcast<3>(d);
        const auto inv_a = hn::Sub(one, a_s);
        const auto new_a = hn::MulAdd(a_d, inv_a, a_s);

        // Combine: RGB from conditional blend, alpha from normal blend
        // Use the same mask inverted on lane 3 to select alpha
        // Reuse rgb_result for lanes 0-2, fix lane 3 with new_a
        hn::StoreU(rgb_result, d4, dst_ptr + i * 4);
        const float corrected_a = hn::GetLane(new_a);
        dst_ptr[i * 4 + 3] = corrected_a > 0.0f ? corrected_a : 0.0f;
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

// ── B2: Canonical premultiplied blend helpers ───────────────────────────────
//
// These implement the full Porter-Duff "over" compositing equation:
//   Co = cb * (1 - As) + cs * (1 - Ab) + B(Cb, Cs) * (As * Ab)
//
// Where cb/cs are premultiplied, Cb/Cs are straight, and B is the blend function.
// This matches blend_math::blend_reference_premul exactly.

// Helper: un-premultiply RGB.  Returns zero for alpha near zero.
HWY_ATTR hn::Vec<hn::FixedTag<float, 4>> safe_unpremul_rgb(
    hn::Vec<hn::FixedTag<float, 4>> premul,
    hn::Vec<hn::FixedTag<float, 4>> alpha_bb,
    const hn::FixedTag<float, 4>& d4,
    const hn::Vec<hn::FixedTag<float, 4>>& epsilon) {
    // Safe division: where alpha <= epsilon, result is 0.
    const auto safe_div = hn::Div(premul, alpha_bb);
    const auto mask = hn::Gt(alpha_bb, epsilon);
    return hn::IfThenElse(mask, safe_div, hn::Zero(d4));
}

HWY_ATTR void composite_darken_premul_impl(float* HWY_RESTRICT dst_ptr,
                                            const float* HWY_RESTRICT src_ptr,
                                            int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);
    const auto eps = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);  // premul src
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);   // premul dst

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);

        // Alpha: Ao = As + Ab - As*Ab = Ab + As*(1 - Ab)
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        // Un-premultiply to straight
        const auto Cs_rgb = safe_unpremul_rgb(s, As, d4, eps);
        const auto Cb_rgb = safe_unpremul_rgb(d, Ab, d4, eps);

        // Blend function: Darken = min(Cb, Cs)
        const auto B = hn::Min(Cb_rgb, Cs_rgb);

        // Porter-Duff over: Co = d * (1-As) + s * (1-Ab) + B * (As * Ab)
        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);

        auto Co = hn::Mul(d, inv_As);             // d * (1-As)
        Co = hn::MulAdd(s, inv_Ab, Co);           // + s * (1-Ab)
        Co = hn::MulAdd(B, AsAb, Co);             // + B * As*Ab

        // Replace alpha.
        hn::StoreU(Co, d4, dst_ptr + i * 4);
        const float corrected_a = hn::GetLane(Ao);
        dst_ptr[i * 4 + 3] = corrected_a > 0.0f ? corrected_a : 0.0f;
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

HWY_ATTR void composite_lighten_premul_impl(float* HWY_RESTRICT dst_ptr,
                                             const float* HWY_RESTRICT src_ptr,
                                             int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);
    const auto eps = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = safe_unpremul_rgb(s, As, d4, eps);
        const auto Cb_rgb = safe_unpremul_rgb(d, Ab, d4, eps);

        // Lighten = max(Cb, Cs)
        const auto B = hn::Max(Cb_rgb, Cs_rgb);

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);

        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        const float corrected_a = hn::GetLane(Ao);
        dst_ptr[i * 4 + 3] = corrected_a > 0.0f ? corrected_a : 0.0f;
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

HWY_ATTR void composite_difference_premul_impl(float* HWY_RESTRICT dst_ptr,
                                                const float* HWY_RESTRICT src_ptr,
                                                int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);
    const auto eps = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = safe_unpremul_rgb(s, As, d4, eps);
        const auto Cb_rgb = safe_unpremul_rgb(d, Ab, d4, eps);

        // Difference = |Cb - Cs|
        const auto diff = hn::Sub(Cb_rgb, Cs_rgb);
        const auto B = hn::Abs(diff);

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);

        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        const float corrected_a = hn::GetLane(Ao);
        dst_ptr[i * 4 + 3] = corrected_a > 0.0f ? corrected_a : 0.0f;
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

HWY_ATTR void composite_exclusion_premul_impl(float* HWY_RESTRICT dst_ptr,
                                               const float* HWY_RESTRICT src_ptr,
                                               int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);
    const auto two = hn::Set(d4, 2.0f);
    const auto eps = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = safe_unpremul_rgb(s, As, d4, eps);
        const auto Cb_rgb = safe_unpremul_rgb(d, Ab, d4, eps);

        // Exclusion = Cb + Cs - 2*Cb*Cs
        const auto prod = hn::Mul(Cb_rgb, Cs_rgb);
        const auto sum = hn::Add(Cb_rgb, Cs_rgb);
        const auto B = hn::Sub(sum, hn::Mul(prod, two));

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);

        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        const float corrected_a = hn::GetLane(Ao);
        dst_ptr[i * 4 + 3] = corrected_a > 0.0f ? corrected_a : 0.0f;
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

// ── B3: SoftLight SIMD ─────────────────────────────────────────────────
HWY_ATTR void composite_soft_light_premul_impl(float* HWY_RESTRICT dst_ptr,
                                                const float* HWY_RESTRICT src_ptr,
                                                int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one  = hn::Set(d4, 1.0f);
    const auto two  = hn::Set(d4, 2.0f);
    const auto half = hn::Set(d4, 0.5f);
    const auto quarter = hn::Set(d4, 0.25f);
    const auto c16 = hn::Set(d4, 16.0f);
    const auto c12 = hn::Set(d4, 12.0f);
    const auto c4  = hn::Set(d4, 4.0f);
    const auto eps = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);           // premul src
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);           // premul dst

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        // Un-premultiply
        const auto Cs_rgb = hn::Div(s, hn::Max(As, eps));
        const auto Cb_rgb = hn::Div(d, hn::Max(Ab, eps));

        // Clamp to [0,1] per HDR contract
        const auto Cs_clamped = hn::Min(hn::Max(Cs_rgb, hn::Zero(d4)), one);
        const auto Cb_clamped = hn::Min(hn::Max(Cb_rgb, hn::Zero(d4)), one);

        // soft_light_d(cb): if cb <= 0.25 → ((16*cb - 12)*cb + 4)*cb  else sqrt(cb)
        auto d_val = hn::Sqrt(Cb_clamped);
        {
            auto poly = hn::Mul(Cb_clamped, hn::Sub(hn::Mul(Cb_clamped, c16), c12));
            poly = hn::Add(poly, c4);
            poly = hn::Mul(poly, Cb_clamped);
            const auto mask = hn::Le(Cb_clamped, quarter);
            d_val = hn::IfThenElse(mask, poly, d_val);
        }

        // soft_light(cb, cs): if cs <= 0.5 → cb - (1-2*cs)*cb*(1-cb)  else cb + (2*cs-1)*(d(cb)-cb)
        const auto om_cs = hn::Sub(one, Cs_clamped);
        const auto branch1 = hn::Sub(Cb_clamped,
                                      hn::Mul(hn::Mul(hn::Sub(one, hn::Mul(two, Cs_clamped)), Cb_clamped),
                                              hn::Sub(one, Cb_clamped)));
        const auto branch2 = hn::Add(Cb_clamped,
                                      hn::Mul(hn::Sub(hn::Mul(two, Cs_clamped), one),
                                              hn::Sub(d_val, Cb_clamped)));
        const auto B = hn::IfThenElse(hn::Le(Cs_clamped, half), branch1, branch2);

        // Porter-Duff over
        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);
        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        dst_ptr[i * 4 + 3] = hn::GetLane(Ao);
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

// ── B3: HardLight SIMD ────────────────────────────────────────────────
HWY_ATTR void composite_hard_light_premul_impl(float* HWY_RESTRICT dst_ptr,
                                                const float* HWY_RESTRICT src_ptr,
                                                int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one  = hn::Set(d4, 1.0f);
    const auto two  = hn::Set(d4, 2.0f);
    const auto half = hn::Set(d4, 0.5f);
    const auto eps  = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = hn::Div(s, hn::Max(As, eps));
        const auto Cb_rgb = hn::Div(d, hn::Max(Ab, eps));

        // Clamp to [0,1]
        const auto Cs_clamped = hn::Min(hn::Max(Cs_rgb, hn::Zero(d4)), one);
        const auto Cb_clamped = hn::Min(hn::Max(Cb_rgb, hn::Zero(d4)), one);

        // if cs <= 0.5 → 2*cb*cs  else 1 - 2*(1-cb)*(1-cs)
        const auto om_cb = hn::Sub(one, Cb_clamped);
        const auto om_cs = hn::Sub(one, Cs_clamped);
        const auto branch1 = hn::Mul(hn::Mul(two, Cb_clamped), Cs_clamped);
        const auto branch2 = hn::Sub(one, hn::Mul(hn::Mul(two, om_cb), om_cs));
        const auto B = hn::IfThenElse(hn::Le(Cs_clamped, half), branch1, branch2);

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);
        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        dst_ptr[i * 4 + 3] = hn::GetLane(Ao);
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

// ── B3: ColorDodge SIMD ───────────────────────────────────────────────
HWY_ATTR void composite_color_dodge_premul_impl(float* HWY_RESTRICT dst_ptr,
                                                 const float* HWY_RESTRICT src_ptr,
                                                 int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one  = hn::Set(d4, 1.0f);
    const auto eps  = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = hn::Div(s, hn::Max(As, eps));
        const auto Cb_rgb = hn::Div(d, hn::Max(Ab, eps));

        // Clamp to [0,1]
        const auto Cs_clamped = hn::Min(hn::Max(Cs_rgb, hn::Zero(d4)), one);
        const auto Cb_clamped = hn::Min(hn::Max(Cb_rgb, hn::Zero(d4)), one);

        // dodge: if cs >= 1 → 1, if cb <= 0 → 0, else min(1, cb/(1-cs))
        const auto om_cs = hn::Sub(one, Cs_clamped);
        const auto dodge_val = hn::Div(Cb_clamped, hn::Max(om_cs, eps));
        const auto dodged = hn::Min(dodge_val, one);

        // cs >= 1 → 1
        const auto cs_ge_1 = hn::Ge(Cs_clamped, one);
        // cb <= 0 → 0
        const auto cb_le_0 = hn::Le(Cb_clamped, hn::Zero(d4));

        auto B = hn::IfThenElse(cb_le_0, hn::Zero(d4), dodged);
        B = hn::IfThenElse(cs_ge_1, one, B);

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);
        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        dst_ptr[i * 4 + 3] = hn::GetLane(Ao);
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

// ── B3: ColorBurn SIMD ────────────────────────────────────────────────
HWY_ATTR void composite_color_burn_premul_impl(float* HWY_RESTRICT dst_ptr,
                                                const float* HWY_RESTRICT src_ptr,
                                                int pixel_count) {
    const hn::FixedTag<float, 4> d4;
    const auto one  = hn::Set(d4, 1.0f);
    const auto eps  = hn::Set(d4, 1e-8f);

    for (int i = 0; i < pixel_count; ++i) {
        if (src_ptr[i * 4 + 3] <= 0.0f) continue;
        const auto s = hn::LoadU(d4, src_ptr + i * 4);
        const auto d = hn::LoadU(d4, dst_ptr + i * 4);

        const auto As = hn::Broadcast<3>(s);
        const auto Ab = hn::Broadcast<3>(d);
        const auto Ao = hn::MulAdd(As, hn::Sub(one, Ab), Ab);

        const auto Cs_rgb = hn::Div(s, hn::Max(As, eps));
        const auto Cb_rgb = hn::Div(d, hn::Max(Ab, eps));

        // Clamp to [0,1]
        const auto Cs_clamped = hn::Min(hn::Max(Cs_rgb, hn::Zero(d4)), one);
        const auto Cb_clamped = hn::Min(hn::Max(Cb_rgb, hn::Zero(d4)), one);

        // burn: if cs <= 0 → 0, if cb >= 1 → 1, else 1 - min(1, (1-cb)/cs)
        const auto om_cb = hn::Sub(one, Cb_clamped);
        const auto burn_val = hn::Div(om_cb, hn::Max(Cs_clamped, eps));
        const auto burned = hn::Sub(one, hn::Min(burn_val, one));

        const auto cs_le_0 = hn::Le(Cs_clamped, hn::Zero(d4));
        const auto cb_ge_1 = hn::Ge(Cb_clamped, one);

        // Match reference (blend_math.hpp): cs <= 0 takes priority over cb >= 1.
        auto B = hn::IfThenElse(cb_ge_1, one, burned);
        B = hn::IfThenElse(cs_le_0, hn::Zero(d4), B);

        const auto inv_As = hn::Sub(one, As);
        const auto inv_Ab = hn::Sub(one, Ab);
        const auto AsAb = hn::Mul(As, Ab);
        auto Co = hn::Mul(d, inv_As);
        Co = hn::MulAdd(s, inv_Ab, Co);
        Co = hn::MulAdd(B, AsAb, Co);

        hn::StoreU(Co, d4, dst_ptr + i * 4);
        dst_ptr[i * 4 + 3] = hn::GetLane(Ao);
        if (dst_ptr[i * 4 + 3] <= 0.0f) {
            dst_ptr[i * 4 + 0] = 0.0f;
            dst_ptr[i * 4 + 1] = 0.0f;
            dst_ptr[i * 4 + 2] = 0.0f;
        }
    }
}

// ── B4: Matte kernels ────────────────────────────────────────────────────

HWY_ATTR void apply_alpha_matte_premul_impl(float* HWY_RESTRICT target_ptr,
                                            const float* HWY_RESTRICT matte_ptr,
                                            int pixel_count,
                                            bool inverted) {
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);
    const auto zero = hn::Zero(d4);

    if (inverted) {
        for (int i = 0; i < pixel_count; ++i) {
            const auto t = hn::LoadU(d4, target_ptr + i * 4);
            const auto m = hn::LoadU(d4, matte_ptr + i * 4);
            const auto matte_alpha = hn::Broadcast<3>(m);
            auto coverage = hn::Sub(one, matte_alpha);  // 1 - ma
            coverage = hn::Min(hn::Max(coverage, zero), one);
            const auto result = hn::Mul(t, coverage);
            hn::StoreU(result, d4, target_ptr + i * 4);
        }
    } else {
        for (int i = 0; i < pixel_count; ++i) {
            const auto t = hn::LoadU(d4, target_ptr + i * 4);
            const auto m = hn::LoadU(d4, matte_ptr + i * 4);
            const auto matte_alpha = hn::Broadcast<3>(m);
            auto coverage = hn::Min(hn::Max(matte_alpha, zero), one);
            const auto result = hn::Mul(t, coverage);
            hn::StoreU(result, d4, target_ptr + i * 4);
        }
    }
}

HWY_ATTR void apply_luma_matte_premul_impl(float* HWY_RESTRICT target_ptr,
                                           const float* HWY_RESTRICT matte_ptr,
                                           int pixel_count,
                                           bool inverted) {
    const hn::FixedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);
    const auto zero = hn::Zero(d4);
    const auto w_r = hn::Set(d4, 0.2126f);
    const auto w_g = hn::Set(d4, 0.7152f);
    const auto w_b = hn::Set(d4, 0.0722f);

    // Compute luma as: 0.2126*r + 0.7152*g + 0.0722*b
    // Using premultiplied channels directly (no extra alpha multiply).
    if (inverted) {
        for (int i = 0; i < pixel_count; ++i) {
            const auto t = hn::LoadU(d4, target_ptr + i * 4);
            const auto m = hn::LoadU(d4, matte_ptr + i * 4);
            // Compute luma in lane 0 via MulAdd chain
            auto luma = hn::Mul(hn::Broadcast<0>(m), w_r);      // r * 0.2126
            luma = hn::MulAdd(hn::Broadcast<1>(m), w_g, luma); // + g * 0.7152
            luma = hn::MulAdd(hn::Broadcast<2>(m), w_b, luma); // + b * 0.0722
            // All lanes now contain luma
            auto coverage = hn::Sub(one, hn::Broadcast<0>(luma));
            coverage = hn::Min(hn::Max(coverage, zero), one);
            const auto result = hn::Mul(t, coverage);
            hn::StoreU(result, d4, target_ptr + i * 4);
        }
    } else {
        for (int i = 0; i < pixel_count; ++i) {
            const auto t = hn::LoadU(d4, target_ptr + i * 4);
            const auto m = hn::LoadU(d4, matte_ptr + i * 4);
            auto luma = hn::Mul(hn::Broadcast<0>(m), w_r);
            luma = hn::MulAdd(hn::Broadcast<1>(m), w_g, luma);
            luma = hn::MulAdd(hn::Broadcast<2>(m), w_b, luma);
            auto coverage = hn::Broadcast<0>(luma);
            coverage = hn::Min(hn::Max(coverage, zero), one);
            const auto result = hn::Mul(t, coverage);
            hn::StoreU(result, d4, target_ptr + i * 4);
        }
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace chronon3d::simd
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace chronon3d::simd {


// ── B4: Safe scalar fallbacks for matte ─────────────────────────────────────

static void apply_alpha_matte_safe(Color* target, const Color* matte, int pixel_count, bool inverted) {
    for (int i = 0; i < pixel_count; ++i) {
        const float ma = matte[i].a;
        const float coverage = inverted ? (1.0f - ma) : ma;
        const float c = std::clamp(coverage, 0.0f, 1.0f);
        target[i].r *= c;
        target[i].g *= c;
        target[i].b *= c;
        target[i].a *= c;
    }
}

static void apply_luma_matte_safe(Color* target, const Color* matte, int pixel_count, bool inverted) {
    for (int i = 0; i < pixel_count; ++i) {
        const float luma = 0.2126f * matte[i].r + 0.7152f * matte[i].g + 0.0722f * matte[i].b;
        const float coverage = inverted ? (1.0f - luma) : luma;
        const float c = std::clamp(coverage, 0.0f, 1.0f);
        target[i].r *= c;
        target[i].g *= c;
        target[i].b *= c;
        target[i].a *= c;
    }
}

// Global flag: force scalar Normal blend for diagnostic purposes
std::atomic<bool> g_force_scalar_normal_blend{false};

HWY_EXPORT(composite_normal_premul_impl);
HWY_EXPORT(composite_add_premul_impl);
HWY_EXPORT(composite_multiply_premul_impl);
HWY_EXPORT(composite_screen_premul_impl);
HWY_EXPORT(composite_overlay_premul_impl);
HWY_EXPORT(composite_darken_premul_impl);
HWY_EXPORT(composite_lighten_premul_impl);
HWY_EXPORT(composite_difference_premul_impl);
HWY_EXPORT(composite_exclusion_premul_impl);
HWY_EXPORT(composite_soft_light_premul_impl);
HWY_EXPORT(composite_hard_light_premul_impl);
HWY_EXPORT(composite_color_dodge_premul_impl);
HWY_EXPORT(composite_color_burn_premul_impl);
HWY_EXPORT(apply_alpha_matte_premul_impl);
HWY_EXPORT(apply_luma_matte_premul_impl);
HWY_EXPORT(premultiply_alpha_rgba8_impl);
HWY_EXPORT(bl_image_prgb32_to_color_row_impl);
HWY_EXPORT(color_to_prgb32_row_impl);

/// Returns true if any channel (r,g,b,a) is NaN or Inf.
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

void composite_normal_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count, bool force_scalar) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count) || force_scalar || g_force_scalar_normal_blend.load(std::memory_order_relaxed)) {
        composite_normal_premul_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_normal_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

// ── Non-normal blend modes (Add, Multiply, Screen, Overlay, Darken, etc.) ──

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
        const float sr = s.r, sg = s.g, sb = s.b;
        const float dr = d.r, dg = d.g, db = d.b;
        d.r = dr < 0.5f ? 2.0f * sr * dr : 1.0f - 2.0f * (1.0f - sr) * (1.0f - dr);
        d.g = dg < 0.5f ? 2.0f * sg * dg : 1.0f - 2.0f * (1.0f - sg) * (1.0f - dg);
        d.b = db < 0.5f ? 2.0f * sb * db : 1.0f - 2.0f * (1.0f - sb) * (1.0f - db);
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

// ── B2 safe scalar fallbacks ─────────────────────────────────────────────

static void composite_darken_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r = std::min(s.r, d.r);
        d.g = std::min(s.g, d.g);
        d.b = std::min(s.b, d.b);
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

static void composite_lighten_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r = std::max(s.r, d.r);
        d.g = std::max(s.g, d.g);
        d.b = std::max(s.b, d.b);
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

static void composite_difference_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r = std::abs(s.r - d.r);
        d.g = std::abs(s.g - d.g);
        d.b = std::abs(s.b - d.b);
        d.a = new_a;
        if (new_a <= 0.0f) { d.r = 0.0f; d.g = 0.0f; d.b = 0.0f; d.a = 0.0f; }
    }
}

static void composite_exclusion_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        d.r = s.r + d.r - 2.0f * s.r * d.r;
        d.g = s.g + d.g - 2.0f * s.g * d.g;
        d.b = s.b + d.b - 2.0f * s.b * d.b;
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

void composite_darken_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_darken_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_darken_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void composite_lighten_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_lighten_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_lighten_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void composite_difference_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_difference_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_difference_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void composite_exclusion_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_exclusion_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_exclusion_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

// ── B3 safe scalar fallbacks ────────────────────────────────────────────────

static void composite_soft_light_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        if (new_a <= 0.0f) { d = Color::transparent(); continue; }
        const float Cs_r = s.a > 1e-8f ? std::min(s.r / s.a, 1.0f) : 0.0f;
        const float Cs_g = s.a > 1e-8f ? std::min(s.g / s.a, 1.0f) : 0.0f;
        const float Cs_b = s.a > 1e-8f ? std::min(s.b / s.a, 1.0f) : 0.0f;
        const float Cb_r = d.a > 1e-8f ? std::min(d.r / d.a, 1.0f) : 0.0f;
        const float Cb_g = d.a > 1e-8f ? std::min(d.g / d.a, 1.0f) : 0.0f;
        const float Cb_b = d.a > 1e-8f ? std::min(d.b / d.a, 1.0f) : 0.0f;
        const float br = blend_math::blend_soft_light(Cb_r, Cs_r);
        const float bg = blend_math::blend_soft_light(Cb_g, Cs_g);
        const float bb = blend_math::blend_soft_light(Cb_b, Cs_b);
        const float inv_As = 1.0f - s.a;
        const float inv_Ab = 1.0f - d.a;
        const float AsAb = s.a * d.a;
        d.r = d.r * inv_As + s.r * inv_Ab + br * AsAb;
        d.g = d.g * inv_As + s.g * inv_Ab + bg * AsAb;
        d.b = d.b * inv_As + s.b * inv_Ab + bb * AsAb;
        d.a = new_a;
    }
}

static void composite_hard_light_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        if (new_a <= 0.0f) { d = Color::transparent(); continue; }
        const float Cs_r = s.a > 1e-8f ? std::min(s.r / s.a, 1.0f) : 0.0f;
        const float Cs_g = s.a > 1e-8f ? std::min(s.g / s.a, 1.0f) : 0.0f;
        const float Cs_b = s.a > 1e-8f ? std::min(s.b / s.a, 1.0f) : 0.0f;
        const float Cb_r = d.a > 1e-8f ? std::min(d.r / d.a, 1.0f) : 0.0f;
        const float Cb_g = d.a > 1e-8f ? std::min(d.g / d.a, 1.0f) : 0.0f;
        const float Cb_b = d.a > 1e-8f ? std::min(d.b / d.a, 1.0f) : 0.0f;
        const float br = blend_math::blend_hard_light(Cb_r, Cs_r);
        const float bg = blend_math::blend_hard_light(Cb_g, Cs_g);
        const float bb = blend_math::blend_hard_light(Cb_b, Cs_b);
        const float inv_As = 1.0f - s.a;
        const float inv_Ab = 1.0f - d.a;
        const float AsAb = s.a * d.a;
        d.r = d.r * inv_As + s.r * inv_Ab + br * AsAb;
        d.g = d.g * inv_As + s.g * inv_Ab + bg * AsAb;
        d.b = d.b * inv_As + s.b * inv_Ab + bb * AsAb;
        d.a = new_a;
    }
}

static void composite_color_dodge_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        if (new_a <= 0.0f) { d = Color::transparent(); continue; }
        const float Cs_r = s.a > 1e-8f ? std::min(s.r / s.a, 1.0f) : 0.0f;
        const float Cs_g = s.a > 1e-8f ? std::min(s.g / s.a, 1.0f) : 0.0f;
        const float Cs_b = s.a > 1e-8f ? std::min(s.b / s.a, 1.0f) : 0.0f;
        const float Cb_r = d.a > 1e-8f ? std::min(d.r / d.a, 1.0f) : 0.0f;
        const float Cb_g = d.a > 1e-8f ? std::min(d.g / d.a, 1.0f) : 0.0f;
        const float Cb_b = d.a > 1e-8f ? std::min(d.b / d.a, 1.0f) : 0.0f;
        const float br = blend_math::blend_color_dodge(Cb_r, Cs_r);
        const float bg = blend_math::blend_color_dodge(Cb_g, Cs_g);
        const float bb = blend_math::blend_color_dodge(Cb_b, Cs_b);
        const float inv_As = 1.0f - s.a;
        const float inv_Ab = 1.0f - d.a;
        const float AsAb = s.a * d.a;
        d.r = d.r * inv_As + s.r * inv_Ab + br * AsAb;
        d.g = d.g * inv_As + s.g * inv_Ab + bg * AsAb;
        d.b = d.b * inv_As + s.b * inv_Ab + bb * AsAb;
        d.a = new_a;
    }
}

static void composite_color_burn_safe(Color* dst, const Color* src, int pixel_count) {
    for (int i = 0; i < pixel_count; ++i) {
        if (has_bad_color(src[i]) || has_bad_color(dst[i])) continue;
        Color& d = dst[i];
        const Color& s = src[i];
        const float new_a = s.a + d.a * (1.0f - s.a);
        if (new_a <= 0.0f) { d = Color::transparent(); continue; }
        const float Cs_r = s.a > 1e-8f ? std::min(s.r / s.a, 1.0f) : 0.0f;
        const float Cs_g = s.a > 1e-8f ? std::min(s.g / s.a, 1.0f) : 0.0f;
        const float Cs_b = s.a > 1e-8f ? std::min(s.b / s.a, 1.0f) : 0.0f;
        const float Cb_r = d.a > 1e-8f ? std::min(d.r / d.a, 1.0f) : 0.0f;
        const float Cb_g = d.a > 1e-8f ? std::min(d.g / d.a, 1.0f) : 0.0f;
        const float Cb_b = d.a > 1e-8f ? std::min(d.b / d.a, 1.0f) : 0.0f;
        const float br = blend_math::blend_color_burn(Cb_r, Cs_r);
        const float bg = blend_math::blend_color_burn(Cb_g, Cs_g);
        const float bb = blend_math::blend_color_burn(Cb_b, Cs_b);
        const float inv_As = 1.0f - s.a;
        const float inv_Ab = 1.0f - d.a;
        const float AsAb = s.a * d.a;
        d.r = d.r * inv_As + s.r * inv_Ab + br * AsAb;
        d.g = d.g * inv_As + s.g * inv_Ab + bg * AsAb;
        d.b = d.b * inv_As + s.b * inv_Ab + bb * AsAb;
        d.a = new_a;
    }
}

void composite_soft_light_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_soft_light_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_soft_light_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void composite_hard_light_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_hard_light_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_hard_light_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void composite_color_dodge_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_color_dodge_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_color_dodge_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void composite_color_burn_premul(Color* __restrict__ dst, const Color* __restrict__ src, int pixel_count) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(dst, src, pixel_count)) {
        composite_color_burn_safe(dst, src, pixel_count);
        return;
    }
    HWY_DYNAMIC_DISPATCH(composite_color_burn_premul_impl)(reinterpret_cast<float*>(dst), reinterpret_cast<const float*>(src), pixel_count);
}

void apply_alpha_matte_premul(Color* __restrict__ target, const Color* __restrict__ matte, int pixel_count, bool inverted) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(target, matte, pixel_count)) {
        apply_alpha_matte_safe(target, matte, pixel_count, inverted);
        return;
    }
    HWY_DYNAMIC_DISPATCH(apply_alpha_matte_premul_impl)(reinterpret_cast<float*>(target), reinterpret_cast<const float*>(matte), pixel_count, inverted);
}

void apply_luma_matte_premul(Color* __restrict__ target, const Color* __restrict__ matte, int pixel_count, bool inverted) {
    if (pixel_count <= 0) return;
    if (!check_nan_canary(target, matte, pixel_count)) {
        apply_luma_matte_safe(target, matte, pixel_count, inverted);
        return;
    }
    HWY_DYNAMIC_DISPATCH(apply_luma_matte_premul_impl)(reinterpret_cast<float*>(target), reinterpret_cast<const float*>(matte), pixel_count, inverted);
}

void premultiply_alpha_rgba8(uint32_t* __restrict__ dst, const uint8_t* __restrict__ src, int pixel_count) {
    HWY_DYNAMIC_DISPATCH(premultiply_alpha_rgba8_impl)(dst, src, pixel_count);
}

void bl_image_prgb32_to_color_row(Color* __restrict__ dst,
                                   const uint32_t* __restrict__ src,
                                   int pixel_count) {
    if (pixel_count <= 0) return;
    HWY_DYNAMIC_DISPATCH(bl_image_prgb32_to_color_row_impl)(
        reinterpret_cast<float*>(dst), src, pixel_count);
}

void color_to_prgb32_row(uint32_t* __restrict__ dst,
                          const Color* __restrict__ src,
                          int pixel_count) {
    if (pixel_count <= 0) return;
    HWY_DYNAMIC_DISPATCH(color_to_prgb32_row_impl)(
        dst, reinterpret_cast<const float*>(src), pixel_count);
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
