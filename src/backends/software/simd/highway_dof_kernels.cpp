// ---------------------------------------------------------------------------
// highway_dof_kernels.cpp — SIMD gather loops for per-pixel DOF blur
//
// Uses Highway multi-target dispatch to accelerate the inner gather loops of
// the separable two-pass variable-radius DOF blur.
//
// Strategy (RGBA-interleaved memory):
//   Color is 4 contiguous floats {r,g,b,a}.  With L SIMD float-lanes we
//   can fit L/4 complete RGBA pixels per vector.  For each output pixel we
//   batch PPB = L/4 neighbor samples:
//     1. LoadU PPB contiguous pixels (= L floats) — one SIMD load.
//     2. Compute per-pixel weights (distance + smoothstep + alpha) as
//        scalars, then broadcast each weight to 4 RGBA lanes.
//     3. SIMD Mul + Add accumulates weighted RGBA.
//     4. Tree-reduce the partial sums to extract {sum_r, sum_g, sum_b, sum_a}.
//
//   For the vertical pass, neighbors are strided by row width so we gather
//   PPB pixel values into a stack buffer, then use the same SIMD pipeline.
//
//   Scalar tail handles remainder when neighbor count < PPB.
// ---------------------------------------------------------------------------

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "src/backends/software/simd/highway_dof_kernels.cpp"
#include <hwy/foreach_target.h>

#include <hwy/highway.h>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/types/types.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

HWY_BEFORE_NAMESPACE();
namespace chronon3d::renderer {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// ── Smoothstep helpers ────────────────────────────────────────────────────
// Inverted smoothstep: weight = 1 at center (t=0), 0 at edge (t=1).
HWY_INLINE float smoothstep_falloff(float t) {
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

HWY_INLINE float compute_weight(float dist, float inv_r, float alpha) {
    if (alpha <= 0.0f) return 0.0f;
    const float t = std::min(dist * inv_r, 1.0f);
    return alpha * smoothstep_falloff(t);
}

// ── SIMD horizontal gather kernel ─────────────────────────────────────────
//
// Processes PPB = Lanes(df)/4 neighbor pixels per SIMD iteration.
// Loads contiguous RGBA data, computes weights (scalar), broadcasts to
// RGBA lanes, accumulates with SIMD Mul+Add.
HWY_ATTR void dof_h_gather_impl(
    const Color* HWY_RESTRICT src_row,
    Color* HWY_RESTRICT dst_row,
    const float* HWY_RESTRICT blur_radii_row,
    int x0, int x1, int w, int y, int fb_w)
{
    using DF = hn::ScalableTag<float>;
    const DF df;
    constexpr int kRGBA = 4;
    const int L  = static_cast<int>(hn::Lanes(df));
    const int PPB = L / kRGBA;  // Pixels per batch (2 on AVX2, 4 on AVX-512)

    for (int x = x0; x < x1; ++x) {
        const size_t idx = static_cast<size_t>(y) * fb_w + x;
        const float my_radius = blur_radii_row[idx];

        if (my_radius < 0.5f) {
            dst_row[x] = src_row[x];
            continue;
        }

        const int r = static_cast<int>(std::ceil(my_radius));
        const float inv_r = 1.0f / my_radius;

        const int sx0 = std::max(0, x - r);
        const int sx1 = std::min(w - 1, x + r);

        // SIMD accumulators: partial sums across PPB pixel groups
        auto v_sum = hn::Zero(df);
        float total_weight = 0.0f;

        int kx = sx0;

        // ── Main SIMD loop: PPB neighbors per iteration ────────────────
        for (; kx + PPB <= sx1 + 1; kx += PPB) {
            // 1. Load PPB contiguous neighbor pixels (L floats, one SIMD load)
            auto v_colors = hn::LoadU(df,
                reinterpret_cast<const float*>(&src_row[kx]));

            // 2. Compute per-pixel weights (scalar), broadcast to 4 RGBA lanes
            alignas(64) float w_broadcast[HWY_MAX_LANES_D(DF)] = {};
            for (int i = 0; i < PPB; ++i) {
                const float dist = std::abs(
                    static_cast<float>(kx + i) - static_cast<float>(x));
                const float w = compute_weight(dist, inv_r, src_row[kx + i].a);
                // Broadcast weight to the 4 RGBA lanes for this pixel
                w_broadcast[i * kRGBA + 0] = w;
                w_broadcast[i * kRGBA + 1] = w;
                w_broadcast[i * kRGBA + 2] = w;
                w_broadcast[i * kRGBA + 3] = w;
                total_weight += w;
            }

            // 3. SIMD: accumulate weighted RGBA
            auto v_weight = hn::Load(df, w_broadcast);
            v_sum = hn::Add(v_sum, hn::Mul(v_colors, v_weight));
        }

        // 4. Reduce partial sums: extract per-channel totals.
        //    We use Store + stride-4 extraction instead of ReduceSum because
        //    ReduceSum sums ALL lanes — we need per-channel (stride-4)
        //    reduction for RGBA-interleaved data.
        alignas(64) float sum_arr[HWY_MAX_LANES_D(DF)];
        hn::Store(v_sum, df, sum_arr);

        float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f, sum_a = 0.0f;
        for (int i = 0; i < PPB; ++i) {
            sum_r += sum_arr[i * kRGBA + 0];
            sum_g += sum_arr[i * kRGBA + 1];
            sum_b += sum_arr[i * kRGBA + 2];
            sum_a += sum_arr[i * kRGBA + 3];
        }

        // ── Scalar tail: remaining neighbors (< PPB) ───────────────────
        for (; kx <= sx1; ++kx) {
            const Color& s = src_row[kx];
            if (s.a <= 0.0f) continue;

            const float dist = std::abs(
                static_cast<float>(kx) - static_cast<float>(x));
            const float w = compute_weight(dist, inv_r, s.a);

            sum_r += s.r * w;
            sum_g += s.g * w;
            sum_b += s.b * w;
            sum_a += s.a * w;
            total_weight += w;
        }

        // ── Normalize ──────────────────────────────────────────────────
        if (total_weight > 1e-6f) {
            const float inv_w = 1.0f / total_weight;
            dst_row[x] = {sum_r * inv_w, sum_g * inv_w,
                          sum_b * inv_w, sum_a * inv_w};
        } else {
            dst_row[x] = src_row[x];
        }
    }
}

// ── SIMD vertical gather kernel ───────────────────────────────────────────
//
// Neighbors are strided by row width (column access).  We gather PPB pixel
// RGBA values into a stack buffer, then use the same SIMD pipeline.
HWY_ATTR void dof_v_gather_impl(
    const Color* HWY_RESTRICT hpass,
    Color* HWY_RESTRICT output,
    const float* HWY_RESTRICT blur_radii,
    int x0, int x1, int y, int h, int fb_w)
{
    using DF = hn::ScalableTag<float>;
    const DF df;
    constexpr int kRGBA = 4;
    const int L  = static_cast<int>(hn::Lanes(df));
    const int PPB = L / kRGBA;

    for (int x = x0; x < x1; ++x) {
        const size_t idx = static_cast<size_t>(y) * fb_w + x;
        const float my_radius = blur_radii[idx];

        if (my_radius < 0.5f) continue;

        const int r = static_cast<int>(std::ceil(my_radius));
        const float inv_r = 1.0f / my_radius;

        const int sy0 = std::max(0, y - r);
        const int sy1 = std::min(h - 1, y + r);

        auto v_sum = hn::Zero(df);
        float total_weight = 0.0f;

        int ky = sy0;

        // ── Main SIMD loop: gather PPB column neighbors per iteration ──
        for (; ky + PPB <= sy1 + 1; ky += PPB) {
            // Gather PPB pixels from strided rows into a contiguous buffer
            alignas(64) Color batch[16]; // PPB max = 4 on AVX-512, 16 is plenty
            for (int i = 0; i < PPB; ++i) {
                batch[i] = hpass[static_cast<size_t>(ky + i) * fb_w + x];
            }

            auto v_colors = hn::LoadU(df,
                reinterpret_cast<const float*>(batch));

            alignas(64) float w_broadcast[HWY_MAX_LANES_D(DF)] = {};
            for (int i = 0; i < PPB; ++i) {
                const float dist = std::abs(
                    static_cast<float>(ky + i) - static_cast<float>(y));
                const float w = compute_weight(dist, inv_r, batch[i].a);
                w_broadcast[i * kRGBA + 0] = w;
                w_broadcast[i * kRGBA + 1] = w;
                w_broadcast[i * kRGBA + 2] = w;
                w_broadcast[i * kRGBA + 3] = w;
                total_weight += w;
            }

            auto v_weight = hn::Load(df, w_broadcast);
            v_sum = hn::Add(v_sum, hn::Mul(v_colors, v_weight));
        }

        // Reduce partial sums
        alignas(64) float sum_arr[HWY_MAX_LANES_D(DF)];
        hn::Store(v_sum, df, sum_arr);

        float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f, sum_a = 0.0f;
        for (int i = 0; i < PPB; ++i) {
            sum_r += sum_arr[i * kRGBA + 0];
            sum_g += sum_arr[i * kRGBA + 1];
            sum_b += sum_arr[i * kRGBA + 2];
            sum_a += sum_arr[i * kRGBA + 3];
        }

        // ── Scalar tail ────────────────────────────────────────────────
        for (; ky <= sy1; ++ky) {
            const Color& s = hpass[static_cast<size_t>(ky) * fb_w + x];
            if (s.a <= 0.0f) continue;

            const float dist = std::abs(
                static_cast<float>(ky) - static_cast<float>(y));
            const float w = compute_weight(dist, inv_r, s.a);

            sum_r += s.r * w;
            sum_g += s.g * w;
            sum_b += s.b * w;
            sum_a += s.a * w;
            total_weight += w;
        }

        // ── Normalize ──────────────────────────────────────────────────
        if (total_weight > 1e-6f) {
            const float inv_w = 1.0f / total_weight;
            output[idx] = {sum_r * inv_w, sum_g * inv_w,
                           sum_b * inv_w, sum_a * inv_w};
        }
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace chronon3d::renderer
HWY_AFTER_NAMESPACE();

// ── Dispatch wrappers ─────────────────────────────────────────────────────
#if HWY_ONCE
namespace chronon3d::renderer {

HWY_EXPORT(dof_h_gather_impl);
HWY_EXPORT(dof_v_gather_impl);

void dof_h_gather_simd(const Color* HWY_RESTRICT src_row,
                       Color* HWY_RESTRICT dst_row,
                       const float* HWY_RESTRICT blur_radii_row,
                       int x0, int x1, int w, int y, int fb_w) {
    HWY_DYNAMIC_DISPATCH(dof_h_gather_impl)(
        src_row, dst_row, blur_radii_row, x0, x1, w, y, fb_w);
}

void dof_v_gather_simd(const Color* HWY_RESTRICT hpass,
                       Color* HWY_RESTRICT output,
                       const float* HWY_RESTRICT blur_radii,
                       int x0, int x1, int y, int h, int fb_w) {
    HWY_DYNAMIC_DISPATCH(dof_v_gather_impl)(
        hpass, output, blur_radii, x0, x1, y, h, fb_w);
}

}  // namespace chronon3d::renderer
#endif
