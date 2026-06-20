#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_blend_kernels.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include <chronon3d/core/memory_utils.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <algorithm>
#include <cmath>

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

// Public wrapper functions: highway_color_kernels.cpp calls these directly.
// The wrappers perform the HWY_DYNAMIC_DISPATCH lookup here, in the translation
// unit where HWY_EXPORT made the dispatch tables visible.
void composite_normal_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                       const float* HWY_RESTRICT src_ptr,
                                       int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_normal_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_add_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                    const float* HWY_RESTRICT src_ptr,
                                    int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_add_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_multiply_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                         const float* HWY_RESTRICT src_ptr,
                                         int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_multiply_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_screen_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                       const float* HWY_RESTRICT src_ptr,
                                       int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_screen_premul_impl)(dst_ptr, src_ptr, pixel_count);
}

void composite_overlay_premul_dispatch(float* HWY_RESTRICT dst_ptr,
                                        const float* HWY_RESTRICT src_ptr,
                                        int pixel_count) {
    HWY_DYNAMIC_DISPATCH(composite_overlay_premul_impl)(dst_ptr, src_ptr, pixel_count);
}
}  // namespace chronon3d::simd
#endif
