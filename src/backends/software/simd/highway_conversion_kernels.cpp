#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_conversion_kernels.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include <chronon3d/core/memory_utils.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

HWY_BEFORE_NAMESPACE();
namespace chronon3d::simd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

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

// ── Matte kernels ────────────────────────────────────────────────────

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
HWY_EXPORT(premultiply_alpha_rgba8_impl);
HWY_EXPORT(bl_image_prgb32_to_color_row_impl);
HWY_EXPORT(color_to_prgb32_row_impl);
HWY_EXPORT(apply_alpha_matte_premul_impl);
HWY_EXPORT(apply_luma_matte_premul_impl);

// Public wrapper functions: highway_color_kernels.cpp calls these directly.
// The wrappers perform the HWY_DYNAMIC_DISPATCH lookup here, in the translation
// unit where HWY_EXPORT made the dispatch tables visible.
void premultiply_alpha_rgba8_dispatch(uint32_t* HWY_RESTRICT dst,
                                       const uint8_t* HWY_RESTRICT src,
                                       int pixel_count) {
    HWY_DYNAMIC_DISPATCH(premultiply_alpha_rgba8_impl)(dst, src, pixel_count);
}

void bl_image_prgb32_to_color_row_dispatch(float* HWY_RESTRICT dst_ptr,
                                            const uint32_t* HWY_RESTRICT src,
                                            int pixel_count) {
    HWY_DYNAMIC_DISPATCH(bl_image_prgb32_to_color_row_impl)(dst_ptr, src, pixel_count);
}

void color_to_prgb32_row_dispatch(uint32_t* HWY_RESTRICT dst,
                                   const float* HWY_RESTRICT src_ptr,
                                   int pixel_count) {
    HWY_DYNAMIC_DISPATCH(color_to_prgb32_row_impl)(dst, src_ptr, pixel_count);
}

void apply_alpha_matte_premul_dispatch(float* HWY_RESTRICT target_ptr,
                                        const float* HWY_RESTRICT matte_ptr,
                                        int pixel_count,
                                        bool inverted) {
    HWY_DYNAMIC_DISPATCH(apply_alpha_matte_premul_impl)(target_ptr, matte_ptr, pixel_count, inverted);
}

void apply_luma_matte_premul_dispatch(float* HWY_RESTRICT target_ptr,
                                       const float* HWY_RESTRICT matte_ptr,
                                       int pixel_count,
                                       bool inverted) {
    HWY_DYNAMIC_DISPATCH(apply_luma_matte_premul_impl)(target_ptr, matte_ptr, pixel_count, inverted);
}
}  // namespace chronon3d::simd
#endif
