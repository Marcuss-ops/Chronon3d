#pragma once

// ============================================================================
// per_pixel_dof.hpp — Per-pixel depth-of-field blur (separable two-pass)
//
// Applies a variable-radius blur to a framebuffer using a per-pixel depth
// buffer. Each output pixel's CoC selects its dense Gaussian kernel, giving
// smooth depth-dependent defocus without scattering neighboring depths into it.
//
// Algorithm: separable two-pass variable-radius blur
//   Pass 1 (horizontal): For each pixel, gather along its row with 1D
//         Gaussian falloff weighted by the output pixel's own blur radius.
//   Pass 2 (vertical):   For each pixel, gather along its column from
//         the horizontally-blurred result, using the same pixel's radius.
//
// Complexity: O(2r) per pixel instead of O(r²) for the disc kernel.
// For a blur radius of 24, this is ~48 vs ~2304 samples per pixel (~48× faster).
//
// Parallelized with TBB over rows for both passes.
// ============================================================================

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <chronon3d/backends/software/effects/dof_simd.hpp>
#include <hwy/highway.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

namespace chronon3d::renderer {

struct DofScratchBuffers {
    std::vector<float> blur_radii;
    std::vector<float> camera_depths;
    std::vector<Color> hpass;
    std::vector<Color> output;
    i32 origin_x{0};
    i32 origin_y{0};
    i32 width{0};
    i32 height{0};

    void ensure_size(i32 x0, i32 y0, i32 w, i32 h) {
        origin_x = x0;
        origin_y = y0;
        width = w;
        height = h;
        const std::size_t pixels = static_cast<std::size_t>(w) * h;
        blur_radii.resize(pixels);
        camera_depths.resize(pixels);
        hpass.resize(pixels);
        output.resize(pixels);
    }
};

/// Apply per-pixel depth-of-field blur to @p fb using @p depth buffer.
///
/// @param fb        Framebuffer to blur in-place.
/// @param depth     Per-pixel positive camera-space depth (width × height, row-major),
///                  passed by non-owning span.  The kernel only reads from it,
///                  so a span is semantically equivalent to a const-ref vector
///                  and avoids the 8 MiB alloc+memcpy that the previous
///                  `std::vector<float>` copy demanded on every dispatch.
///                  Callers that own a `std::vector<float>` may construct
///                  the span implicitly (C++20 contiguous_range constructor)
///                  or use the explicit `std::span<const float>{v}` form.
/// @param dof       Camera DOF settings (focus_distance, max_blur).
/// @param lens      Physical lens model (focal_length, sensor_width, f_stop).
/// @param clip      Optional clip rectangle; only pixels inside are blurred.
///
/// The depth buffer must be the same size as the framebuffer (width × height).
/// Pixels with depth == kUnsetDofDepth are treated as "no data" and left unblurred.
inline void apply_per_pixel_dof(
    Framebuffer& fb,
    std::span<const float> depth,
    const DepthOfFieldSettings& dof,
    const LensModel& lens,
    const std::optional<raster::BBox>& clip = std::nullopt,
    DofScratchBuffers* scratch = nullptr)
{
    if (!dof.enabled) return;

    const auto add_us = [](std::atomic<uint64_t>& counter,
                           chronon3d::profiling::Clock::time_point start) {
        counter.fetch_add(static_cast<uint64_t>(
                              std::max(0.0, chronon3d::profiling::elapsed_us(start))),
                          std::memory_order_relaxed);
    };

    const i32 w = fb.width();
    const i32 h = fb.height();

    if (static_cast<i32>(depth.size()) != w * h) return;

    // Determine processing bounds
    i32 x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w);
        x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h);
        y1 = std::clamp(clip->y1, 0, h);
    }
    if (x0 >= x1 || y0 >= y1) return;

    const auto radius_start = chronon3d::profiling::now();

    // Pre-compute per-pixel blur radii from the depth buffer.
    // Negative radius is a separate "no depth" sentinel. A zero radius is
    // a valid in-focus surface and must remain eligible as a neighbour for
    // another out-of-focus pixel; treating both as zero reintroduces opaque
    // background samples into the text blur and creates bbox-shaped patches.
    DofScratchBuffers local_scratch;
    auto* buffers = scratch != nullptr ? scratch : &local_scratch;
    buffers->ensure_size(x0, y0, x1 - x0, y1 - y0);
    const i32 roi_w = x1 - x0;
    const i32 roi_h = y1 - y0;
    auto& blur_radii = buffers->blur_radii;
    auto& camera_depths = buffers->camera_depths;
    std::fill(blur_radii.begin(), blur_radii.end(), -1.0f);
    std::fill(camera_depths.begin(), camera_depths.end(), -1.0f);
    float max_r = 0.0f;
    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            const size_t idx = static_cast<size_t>(y - y0) * roi_w + (x - x0);
            const float z = depth[static_cast<size_t>(y) * w + x];
            if (std::isfinite(z) && z > 0.0f &&
                z < kUnsetDofDepth * 0.5f) {
                blur_radii[idx] = compute_dof_blur_radius(dof, lens, z);
                camera_depths[idx] = z;
            }
            max_r = std::max(max_r, blur_radii[idx]);
        }
    }
    if (chronon3d::profiling::g_current_counters) {
        add_us(chronon3d::profiling::g_current_counters->dof_blur_radius_generation_wall_us,
               radius_start);
        chronon3d::profiling::g_current_counters->dof_roi_pixels.fetch_add(
            static_cast<uint64_t>(x1 - x0) * static_cast<uint64_t>(y1 - y0),
            std::memory_order_relaxed);
        chronon3d::profiling::g_current_counters->dof_max_radius_milli.fetch_add(
            static_cast<uint64_t>(std::lround(std::max(0.0f, max_r) * 1000.0f)),
            std::memory_order_relaxed);
    }
    if (max_r < 0.5f) return; // No visible blur

    // The blur is source-driven: a defocused source pixel contributes to
    // nearby destinations, while a focused background pixel contributes only
    // at its own coordinate. This is the separable form of a scatter blur and
    // avoids assigning the maximum radius to an entire destination rectangle.
    // Scratch buffers for the two-pass separable blur.
    // `hpass` stores the horizontally-blurred result; `output` stores the final.
    // Both are allocated only for the processing ROI.
    const auto scratch_start = chronon3d::profiling::now();
    auto& hpass = buffers->hpass;
    auto& output = buffers->output;
    if (chronon3d::profiling::g_current_counters) {
        add_us(chronon3d::profiling::g_current_counters->dof_scratch_allocation_wall_us,
               scratch_start);
        const uint64_t scratch_bytes =
            static_cast<uint64_t>(roi_w) * static_cast<uint64_t>(roi_h) * sizeof(Color) * 2;
        chronon3d::profiling::g_current_counters->dof_scratch_bytes.fetch_add(
            scratch_bytes, std::memory_order_relaxed);
    }

    // Copy only the ROI into hpass; pixels outside it remain untouched in fb.
    const auto copy_start = chronon3d::profiling::now();
    for (i32 y = y0; y < y1; ++y) {
        const Color* src_row = fb.pixels_row(y);
        std::copy_n(src_row + x0, roi_w,
                    &hpass[static_cast<size_t>(y - y0) * roi_w]);
    }
    if (chronon3d::profiling::g_current_counters) {
        add_us(chronon3d::profiling::g_current_counters->dof_copy_to_hpass_wall_us,
               copy_start);
        chronon3d::profiling::g_current_counters->dof_estimated_bytes_read.fetch_add(
            static_cast<uint64_t>(roi_w) * static_cast<uint64_t>(roi_h) * sizeof(Color),
            std::memory_order_relaxed);
        chronon3d::profiling::g_current_counters->dof_estimated_bytes_written.fetch_add(
            static_cast<uint64_t>(roi_w) * static_cast<uint64_t>(roi_h) * sizeof(Color),
            std::memory_order_relaxed);
    }

    // ── Pass 1: Horizontal blur (TBB parallel over rows) ───────────────
    // SIMD-accelerated: inner gather loop processes multiple neighbors at
    // once using Highway SIMD vectors (see dof_simd.hpp / dof_h_gather_simd).
    const auto horizontal_start = chronon3d::profiling::now();
    tbb::parallel_for(tbb::blocked_range<i32>(y0, y1),
        [&](const tbb::blocked_range<i32>& range) {
            for (i32 y = range.begin(); y < range.end(); ++y) {
                const Color* src_row = fb.pixels_row(y);
                Color* dst_row = &hpass[static_cast<size_t>(y - y0) * roi_w];
                const float* radius_row =
                    &blur_radii[static_cast<size_t>(y - y0) * roi_w];
                const float* depth_row =
                    &camera_depths[static_cast<size_t>(y - y0) * roi_w];

                dof_h_gather_simd(src_row, dst_row, radius_row, depth_row, max_r,
                                  fb.is_opaque(),
                                  std::max(0.0f, dof.highlight_gain),
                                  x0, x1, w, y, w, x0, x1);
            }
        }
    );
    if (chronon3d::profiling::g_current_counters) {
        add_us(chronon3d::profiling::g_current_counters->dof_horizontal_pass_wall_us,
               horizontal_start);
    }

    // Copy hpass into output as the starting point for vertical pass
    // (non-clipped rows stay as horizontally-blurred result)
    const auto hpass_copy_start = chronon3d::profiling::now();
    std::copy(hpass.begin(), hpass.end(), output.begin());
    if (chronon3d::profiling::g_current_counters) {
        add_us(chronon3d::profiling::g_current_counters->dof_hpass_to_output_wall_us,
               hpass_copy_start);
        chronon3d::profiling::g_current_counters->dof_estimated_bytes_read.fetch_add(
            static_cast<uint64_t>(roi_w) * static_cast<uint64_t>(roi_h) * sizeof(Color),
            std::memory_order_relaxed);
        chronon3d::profiling::g_current_counters->dof_estimated_bytes_written.fetch_add(
            static_cast<uint64_t>(roi_w) * static_cast<uint64_t>(roi_h) * sizeof(Color),
            std::memory_order_relaxed);
    }

    // ── Pass 2: Vertical blur (2D tiled for cache locality) ─────────────
    // The vertical pass gathers from strided rows (column access).  Adjacent
    // output columns share the same source row range in hpass.  By processing
    // 2D tiles of (rows × columns), we keep the hpass window for each tile
    // hot in L1/L2 cache.
    //
    // Tile sizing: 32 columns × 64 rows.  Working set per tile ≈
    //   32 × (64 + 2·max_r) × 16 bytes.  For max_r=24: 32 × 112 × 16 = 56 KB
    //   → fits in L2.  For max_r=48: 32 × 160 × 16 = 80 KB → fits in L2.
    //
    // Parallelism: for 1920×1080, we get ceil(1920/32) × ceil(1080/64) =
    // 60 × 17 = 1020 tiles — plenty of work for all cores.
    //
    // We enumerate (col_tile, row_tile) pairs manually since
    // blocked_range2d is not available in this TBB version.
    constexpr i32 kTileCols = 32;
    constexpr i32 kTileRows = 64;

    // Build tile list: (col_start, row_start) pairs
    std::vector<i32> col_starts;
    for (i32 cx = x0; cx < x1; cx += kTileCols)
        col_starts.push_back(cx);
    const i32 nct = static_cast<i32>(col_starts.size());

    std::vector<i32> row_starts;
    for (i32 ry = y0; ry < y1; ry += kTileRows)
        row_starts.push_back(ry);
    const i32 nrt = static_cast<i32>(row_starts.size());

    const i32 num_tiles = nct * nrt;

    const auto vertical_start = chronon3d::profiling::now();
    tbb::parallel_for(tbb::blocked_range<i32>(0, num_tiles),
        [&](const tbb::blocked_range<i32>& range) {
            for (i32 ti = range.begin(); ti < range.end(); ++ti) {
                const i32 ci = ti / nrt;
                const i32 ri = ti % nrt;
                const i32 tx0 = col_starts[ci];
                const i32 tx1 = std::min(tx0 + kTileCols, x1);
                const i32 ty0 = row_starts[ri];
                const i32 ty1 = std::min(ty0 + kTileRows, y1);

                for (i32 y = ty0; y < ty1; ++y) {
                    dof_v_gather_simd(hpass.data(), output.data(),
                                      blur_radii.data(), camera_depths.data(), max_r,
                                      fb.is_opaque(),
                                      std::max(0.0f, dof.highlight_gain),
                                      tx0, tx1, y, h, w,
                                      x0, x1, y0, y1, roi_w);
                }
            }
        }
    );
    if (chronon3d::profiling::g_current_counters) {
        add_us(chronon3d::profiling::g_current_counters->dof_vertical_pass_wall_us,
               vertical_start);
    }

    // ── Write result back to framebuffer (parallel + SIMD) ─────────────
    const auto writeback_start = chronon3d::profiling::now();
    tbb::parallel_for(tbb::blocked_range<i32>(y0, y1),
        [&](const tbb::blocked_range<i32>& range) {
            using namespace hwy::HWY_NAMESPACE;
            const ScalableTag<float> df;
            const size_t lanes = Lanes(df);
            const int floats_per_row = roi_w * 4;

            for (i32 y = range.begin(); y < range.end(); ++y) {
                const float* src = reinterpret_cast<const float*>(
                    &output[static_cast<size_t>(y - y0) * roi_w]);
                float* dst = reinterpret_cast<float*>(fb.pixels_row(y) + x0);

                int fx = 0;
                for (; fx + static_cast<int>(lanes) <= floats_per_row; fx += static_cast<int>(lanes)) {
                    StoreU(LoadU(df, src + fx), df, dst + fx);
                }
                for (; fx < floats_per_row; ++fx) {
                    dst[fx] = src[fx];
                }
            }
        }
    );
    if (chronon3d::profiling::g_current_counters) {
        add_us(chronon3d::profiling::g_current_counters->dof_writeback_wall_us,
               writeback_start);
        chronon3d::profiling::g_current_counters->dof_estimated_bytes_read.fetch_add(
            static_cast<uint64_t>(roi_w) * static_cast<uint64_t>(roi_h) * sizeof(Color),
            std::memory_order_relaxed);
        chronon3d::profiling::g_current_counters->dof_estimated_bytes_written.fetch_add(
            static_cast<uint64_t>(x1 - x0) * static_cast<uint64_t>(y1 - y0) * sizeof(Color),
            std::memory_order_relaxed);
    }
}

// ── Disk (bokeh) kernel — CPU mirror of dof_blur.comp `disk_blur` ──────────
//
// The separable pass above is a soft rectangle support. Per-layer optical
// defocus uses the disk kernel instead, and this implementation must stay
// bit-for-bit equivalent in its *weighting* to the Vulkan shader
// (`disk_edge_weight`, `dof_disk_pair_weight`, `highlight_weight`): the two
// lanes render the same accepted scene, so a different weighting is a parity
// defect, not a stylistic choice.

/// 1 inside the disk of @p radius, smoothly falling to 0 one pixel outside.
[[nodiscard]] HWY_INLINE float dof_disk_edge_weight(float dist, float radius) {
    if (radius < 0.5f || dist > radius + 0.5f) return 0.0f;
    const float t = std::clamp(dist - std::max(0.0f, radius - 0.5f), 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

/// Radial metric of a regular n-gon aperture, in units of the authored CoC
/// radius. The polygon is inscribed in that circle (a vertex lies on the +x
/// axis, flat sides at the apothem), so a sample is inside when this metric is
/// <= radius. `blades < 3` keeps the Euclidean circle (the historical kernel).
/// Mirrors `aperture_metric()` in dof_blur.comp: the software and Vulkan lanes
/// must agree on the aperture.
[[nodiscard]] HWY_INLINE float dof_aperture_metric(
    float dx, float dy, int blades) {
    const float euclidean = std::sqrt(dx * dx + dy * dy);
    if (blades < 3) return euclidean;
    constexpr float kPi = 3.14159265358979323846f;
    const float segment = 2.0f * kPi / static_cast<float>(blades);
    const float angle = std::atan2(dy, dx);
    const float offset =
        std::fmod(angle + segment * 0.5f, segment) - segment * 0.5f;
    return euclidean * std::cos(offset);
}

/// Depth- and CoC-aware disk pair weight (same predicate as the shader).
[[nodiscard]] HWY_INLINE float dof_disk_pair_weight(
    float dist, float centre_radius, float centre_depth,
    float neighbour_radius, float neighbour_depth) {
    if (neighbour_radius < 0.0f) return 0.0f;
    if (centre_radius >= 0.0f && centre_radius < 0.5f) {
        if (dist == 0.0f) return 1.0f;
        if (neighbour_radius >= 0.5f && neighbour_depth > 0.0f &&
            centre_depth > 0.0f && neighbour_depth + 1e-3f < centre_depth) {
            return dof_disk_edge_weight(dist, neighbour_radius);
        }
        return 0.0f;
    }
    if (neighbour_radius < 0.5f) return dist == 0.0f ? 1.0f : 0.0f;
    return dof_disk_edge_weight(dist, neighbour_radius);
}

/// Out-of-focus highlight weighting: samples brighter than 1.0 (HDR) gain
/// extra weight so bokeh keeps its energy instead of averaging grey.
[[nodiscard]] HWY_INLINE float dof_highlight_weight(
    const Color& sample, float highlight_gain) {
    const float luminance =
        0.2126f * sample.r + 0.7152f * sample.g + 0.0722f * sample.b;
    return 1.0f + highlight_gain * std::max(luminance - 1.0f, 0.0f);
}

/// Per-layer disk blur of @p fb inside `[roi_x0, roi_x1) x [roi_y0, roi_y1)`.
///
/// @param depth / @param radii  Per-pixel camera depth and precomputed CoC
///        radius over the *whole* framebuffer, `width * height` samples.
///        A negative radius is the "no depth" sentinel (sample ignored).
///        `depth == kUnsetDofDepth` marks an uncovered pixel.
/// @param highlight_gain Authored highlight energy gain (>= 0).
///
/// Pixels outside the ROI are left untouched, exactly like the shader's early
/// return. A pixel whose own radius is negative is copied through unchanged.
inline void apply_per_pixel_dof_disk(
    Framebuffer& fb,
    std::span<const float> depth,
    std::span<const float> radii,
    i32 roi_x0, i32 roi_y0, i32 roi_x1, i32 roi_y1,
    f32 max_radius,
    bool normalize_opaque,
    f32 highlight_gain,
    int iris_blades = 0)
{
    const i32 w = fb.width();
    const i32 h = fb.height();
    if (w <= 0 || h <= 0) return;
    const std::size_t pixels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    if (depth.size() != pixels || radii.size() != pixels) return;
    roi_x0 = std::clamp(roi_x0, 0, w);
    roi_x1 = std::clamp(roi_x1, roi_x0, w);
    roi_y0 = std::clamp(roi_y0, 0, h);
    roi_y1 = std::clamp(roi_y1, roi_y0, h);
    if (roi_x0 >= roi_x1 || roi_y0 >= roi_y1) return;
    const f32 gain = std::max(0.0f, highlight_gain);
    const i32 radius_px = static_cast<i32>(std::ceil(std::max(0.0f, max_radius)));
    if (radius_px <= 0) return;

    // Gather from an immutable copy so writes never feed later samples.
    std::vector<Color> source(pixels);
    for (i32 y = 0; y < h; ++y) {
        std::memcpy(source.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w),
                    fb.pixels_row(y), static_cast<std::size_t>(w) * sizeof(Color));
    }

    tbb::parallel_for(tbb::blocked_range<i32>(roi_y0, roi_y1),
        [&](const tbb::blocked_range<i32>& rows) {
        for (i32 y = rows.begin(); y < rows.end(); ++y) {
            for (i32 x = roi_x0; x < roi_x1; ++x) {
                const std::size_t centre_idx =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                    static_cast<std::size_t>(x);
                const Color centre = source[centre_idx];
                const float centre_radius = radii[centre_idx];
                if (centre_radius < 0.0f) {
                    fb.pixels_row(y)[x] = centre;
                    continue;
                }
                const float centre_depth = depth[centre_idx];
                const i32 lx = std::max(roi_x0, x - radius_px);
                const i32 hx = std::min(roi_x1 - 1, x + radius_px);
                const i32 ly = std::max(roi_y0, y - radius_px);
                const i32 hy = std::min(roi_y1 - 1, y + radius_px);
                float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f, sum_a = 0.0f;
                float total_weight = 0.0f;
                for (i32 sy = ly; sy <= hy; ++sy) {
                    const Color* row = source.data() +
                        static_cast<std::size_t>(sy) * static_cast<std::size_t>(w);
                    const float* depth_row = depth.data() +
                        static_cast<std::size_t>(sy) * static_cast<std::size_t>(w);
                    const float* radii_row = radii.data() +
                        static_cast<std::size_t>(sy) * static_cast<std::size_t>(w);
                    for (i32 sx = lx; sx <= hx; ++sx) {
                        const float sample_radius = radii_row[sx];
                        if (sample_radius < 0.0f) continue;
                        const float dx = static_cast<float>(sx - x);
                        const float dy = static_cast<float>(sy - y);
                        const float dist = dof_aperture_metric(dx, dy, iris_blades);
                        float weight = dof_disk_pair_weight(
                            dist, centre_radius, centre_depth,
                            sample_radius, depth_row[sx]);
                        if (weight <= 0.0f) continue;
                        const Color& sample = row[sx];
                        if (normalize_opaque) weight *= sample.a;
                        weight *= dof_highlight_weight(sample, gain);
                        sum_r += sample.r * weight;
                        sum_g += sample.g * weight;
                        sum_b += sample.b * weight;
                        sum_a += sample.a * weight;
                        total_weight += weight;
                    }
                }
                fb.pixels_row(y)[x] = total_weight > 1e-6f
                    ? Color{sum_r / total_weight, sum_g / total_weight,
                            sum_b / total_weight, sum_a / total_weight}
                    : centre;
            }
        }
    });
}

} // namespace chronon3d::renderer
