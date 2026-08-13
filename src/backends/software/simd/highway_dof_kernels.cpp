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

HWY_INLINE float compute_weight(float dist, float inv_r) {
    const float t = std::min(dist * inv_r, 1.0f);
    return smoothstep_falloff(t);
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
    float max_radius,
    bool normalize_opaque,
    int x0, int x1, int w, int y, int fb_w, int roi_x0, int roi_x1)
{
    using DF = hn::ScalableTag<float>;
    const DF df;
    constexpr int kRGBA = 4;
    const int L  = static_cast<int>(hn::Lanes(df));
    const int PPB = L / kRGBA;  // Pixels per batch (2 on AVX2, 4 on AVX-512)

    for (int x = x0; x < x1; ++x) {
        const size_t local_x = static_cast<size_t>(x - roi_x0);
        const size_t dst_idx = local_x;
        // In-focus destinations remain exact. Defocused source pixels still
        // spread into defocused destinations; this preserves sharp text edges
        // on transparent projected surfaces.
        if (!normalize_opaque && src_row[x].a > 0.0f &&
            blur_radii_row[dst_idx] >= 0.0f &&
            blur_radii_row[dst_idx] < 0.5f) {
            dst_row[local_x] = src_row[x];
            continue;
        }
        const int r = static_cast<int>(std::ceil(max_radius));

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
                if (kx + i < roi_x0 || kx + i >= roi_x1) continue;
                const float dist = std::abs(
                    static_cast<float>(kx + i) - static_cast<float>(x));
                const float neighbour_radius = blur_radii_row[
                    static_cast<size_t>(kx + i - roi_x0)];
                const float kernel_w = neighbour_radius < 0.0f ? 0.0f
                    : (neighbour_radius < 0.5f
                        ? (dist == 0.0f ? 1.0f : 0.0f)
                        : compute_weight(dist, 1.0f / neighbour_radius));
                const float w = normalize_opaque
                    ? kernel_w * src_row[kx + i].a : kernel_w;
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
            if (kx < roi_x0 || kx >= roi_x1) continue;
            const Color& s = src_row[kx];
            if (blur_radii_row[static_cast<size_t>(kx - roi_x0)] < 0.0f) continue;
            const float dist = std::abs(
                static_cast<float>(kx) - static_cast<float>(x));
            const float source_radius = blur_radii_row[
                static_cast<size_t>(kx - roi_x0)];
            const float kernel_w = source_radius < 0.5f
                ? (dist == 0.0f ? 1.0f : 0.0f)
                : compute_weight(dist, 1.0f / source_radius);
            const float w = normalize_opaque ? kernel_w * s.a : kernel_w;

            sum_r += s.r * w;
            sum_g += s.g * w;
            sum_b += s.b * w;
            sum_a += s.a * w;
            total_weight += w;
        }

        // ── Normalize ──────────────────────────────────────────────────
        if (total_weight > 1e-6f) {
            const float inv_w = 1.0f / total_weight;
            dst_row[local_x] = {sum_r * inv_w, sum_g * inv_w,
                          sum_b * inv_w, sum_a * inv_w};
        } else {
            dst_row[local_x] = src_row[x];
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
    float max_radius,
    bool normalize_opaque,
    int x0, int x1, int y, int h, int fb_w,
    int roi_x0, int roi_x1, int roi_y0, int roi_y1, int roi_w)
{
    using DF = hn::ScalableTag<float>;
    const DF df;
    constexpr int kRGBA = 4;
    const int L  = static_cast<int>(hn::Lanes(df));
    const int PPB = L / kRGBA;

    for (int x = x0; x < x1; ++x) {
        const size_t idx = static_cast<size_t>(y - roi_y0) * roi_w +
                           static_cast<size_t>(x - roi_x0);
        if (!normalize_opaque && hpass[idx].a > 0.0f &&
            blur_radii[idx] >= 0.0f && blur_radii[idx] < 0.5f) {
            output[idx] = hpass[idx];
            continue;
        }
        const int r = static_cast<int>(std::ceil(max_radius));

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
                if (ky + i < roi_y0 || ky + i >= roi_y1) {
                    batch[i] = {};
                    continue;
                }
                batch[i] = hpass[static_cast<size_t>(ky + i - roi_y0) * roi_w +
                                 static_cast<size_t>(x - roi_x0)];
            }

            auto v_colors = hn::LoadU(df,
                reinterpret_cast<const float*>(batch));

            alignas(64) float w_broadcast[HWY_MAX_LANES_D(DF)] = {};
            for (int i = 0; i < PPB; ++i) {
                if (ky + i < roi_y0 || ky + i >= roi_y1) continue;
                const float dist = std::abs(
                    static_cast<float>(ky + i) - static_cast<float>(y));
                const float neighbour_radius = blur_radii[
                    static_cast<size_t>(ky + i - roi_y0) * roi_w +
                    static_cast<size_t>(x - roi_x0)];
                const float kernel_w = neighbour_radius < 0.0f ? 0.0f
                    : (neighbour_radius < 0.5f
                        ? (dist == 0.0f ? 1.0f : 0.0f)
                        : compute_weight(dist, 1.0f / neighbour_radius));
                const float w = normalize_opaque
                    ? kernel_w * batch[i].a : kernel_w;
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
            if (ky < roi_y0 || ky >= roi_y1) continue;
            const Color& s = hpass[static_cast<size_t>(ky - roi_y0) * roi_w +
                                   static_cast<size_t>(x - roi_x0)];
            if (blur_radii[static_cast<size_t>(ky - roi_y0) * roi_w +
                           static_cast<size_t>(x - roi_x0)] < 0.0f) continue;
            const float dist = std::abs(
                static_cast<float>(ky) - static_cast<float>(y));
            const float source_radius = blur_radii[
                static_cast<size_t>(ky - roi_y0) * roi_w +
                static_cast<size_t>(x - roi_x0)];
            const float kernel_w = source_radius < 0.5f
                ? (dist == 0.0f ? 1.0f : 0.0f)
                : compute_weight(dist, 1.0f / source_radius);
            const float w = normalize_opaque ? kernel_w * s.a : kernel_w;

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
                       float max_radius,
                       bool normalize_opaque,
                       int x0, int x1, int w, int y, int fb_w,
                       int roi_x0, int roi_x1) {
    HWY_DYNAMIC_DISPATCH(dof_h_gather_impl)(
        src_row, dst_row, blur_radii_row, max_radius, normalize_opaque,
        x0, x1, w, y, fb_w, roi_x0, roi_x1);
}

void dof_v_gather_simd(const Color* HWY_RESTRICT hpass,
                       Color* HWY_RESTRICT output,
                       const float* HWY_RESTRICT blur_radii,
                       float max_radius,
                       bool normalize_opaque,
                       int x0, int x1, int y, int h, int fb_w,
                       int roi_x0, int roi_x1, int roi_y0, int roi_y1,
                       int roi_w) {
    HWY_DYNAMIC_DISPATCH(dof_v_gather_impl)(
        hpass, output, blur_radii, max_radius, normalize_opaque,
        x0, x1, y, h, fb_w, roi_x0, roi_x1, roi_y0, roi_y1, roi_w);
}

}  // namespace chronon3d::renderer
#endif
