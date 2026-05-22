// ---------------------------------------------------------------------------
// Highway multi-target SIMD kernels.
//
// foreach_target.h re-includes this translation unit once per available SIMD
// target (AVX2, SSE4, etc.), each time defining a unique HWY_TARGET and
// compiling inside a unique namespace (hwy::N_AVX2, hwy::N_SSE4, etc.).
// The HWY_ONCE section defines the public dispatch function.
//
// Pattern from: hwy/examples/skeleton.cc (Highway 1.3)
// ---------------------------------------------------------------------------

// Must be defined before foreach_target.h so the re-inclusion directive works.
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_kernels.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

// ---------------------------------------------------------------------------
// Per-target section — compiled once per enabled SIMD target.
// ---------------------------------------------------------------------------

HWY_BEFORE_NAMESPACE();
namespace chronon3d {
namespace simd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// composite_normal_premul — 1 pixel (4 f32) per iteration via CappedTag<4>.
//
// src.rgb is premultiplied by src.a.
//   dst = src + dst * (1 - src.a)
//
// The per-pixel loop naturally handles any pixel_count; no tail needed since
// CappedTag<float,4> advances exactly 1 pixel per iteration.

HWY_ATTR void composite_normal_premul_impl(float* HWY_RESTRICT dst,
                                            const float* HWY_RESTRICT src,
                                            int pixel_count) {
    const hn::CappedTag<float, 4> d4;
    const auto one = hn::Set(d4, 1.0f);

    for (int i = 0; i < pixel_count; ++i) {
        const float* s_ptr = src + i * 4;
        const float alpha = s_ptr[3];
        if (alpha <= 0.001f) {
            continue;
        }
        float* d_ptr = dst + i * 4;
        if (alpha >= 0.999f) {
            d_ptr[0] = s_ptr[0];
            d_ptr[1] = s_ptr[1];
            d_ptr[2] = s_ptr[2];
            d_ptr[3] = s_ptr[3];
            continue;
        }

        auto s = hn::LoadU(d4, s_ptr);
        auto d = hn::LoadU(d4, d_ptr);

        // Broadcast alpha (lane 3) to all lanes: [a, a, a, a]
        const auto s_alpha = hn::Broadcast<3>(s);

        // inv_a = 1.0f - a
        const auto inv_a = hn::Sub(one, s_alpha);

        // Premultiplied alpha OVER blend:
        // dst = src + dst * (1 - src.a)
        const auto dst_scaled = hn::Mul(d, inv_a);
        const auto blended = hn::Add(s, dst_scaled);

        hn::StoreU(blended, d4, d_ptr);
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace simd
}  // namespace chronon3d
HWY_AFTER_NAMESPACE();

// ---------------------------------------------------------------------------
// Once section — compiled once, defines the public API.
// ---------------------------------------------------------------------------

#if HWY_ONCE

#include <chronon3d/simd/kernels.hpp>
#include <algorithm>

namespace chronon3d {
namespace simd {

HWY_EXPORT(composite_normal_premul_impl);

void composite_normal_premul(Color* __restrict__ dst,
                              const Color* __restrict__ src,
                              int pixel_count) {
    // Highway's dynamic dispatch selects the best SIMD target at runtime.
    // If only one target is available, HWY_EXPORT optimises to a direct call.
    HWY_DYNAMIC_DISPATCH(composite_normal_premul_impl)(
        reinterpret_cast<float*>(dst),
        reinterpret_cast<const float*>(src),
        pixel_count);
}

// clear_framebuffer: std::fill is auto-vectorized by modern compilers for
// trivially copyable 16-byte Color structs.
void clear_framebuffer(Color* data, int pixel_count, const Color& color) {
    std::fill(data, data + pixel_count, color);
}

}  // namespace simd
}  // namespace chronon3d

#endif  // HWY_ONCE
