#pragma once

// ============================================================================
// per_pixel_dof.hpp — Per-pixel depth-of-field blur (separable two-pass)
//
// Applies a variable-radius blur to a framebuffer using a per-pixel depth
// buffer.  Each pixel's blur radius is computed from its world_z via
// compute_dof_blur_radius(), giving smooth depth-dependent defocus.
//
// Algorithm: separable two-pass variable-radius blur
//   Pass 1 (horizontal): For each pixel, gather along its row with 1D
//         smoothstep falloff weighted by the pixel's own blur radius.
//   Pass 2 (vertical):   For each pixel, gather along its column from
//         the horizontally-blurred result, same weighting.
//
// Complexity: O(2r) per pixel instead of O(r²) for the disc kernel.
// For a blur radius of 24, this is ~48 vs ~2304 samples per pixel (~48× faster).
//
// Parallelized with TBB over rows for both passes.
// ============================================================================

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/camera/dof.hpp>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <chronon3d/backends/software/utils/effects/dof_simd.hpp>
#include <hwy/highway.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace chronon3d::renderer {

/// Apply per-pixel depth-of-field blur to @p fb using @p depth buffer.
///
/// @param fb        Framebuffer to blur in-place.
/// @param depth     Per-pixel world_z buffer (width × height, row-major).
/// @param dof       Camera DOF settings (focus_z, aperture, max_blur).
/// @param clip      Optional clip rectangle; only pixels inside are blurred.
///
/// The depth buffer must be the same size as the framebuffer (width × height).
/// Pixels with depth == kUnsetDepth are treated as "no data" and left unblurred.
inline void apply_per_pixel_dof(
    Framebuffer& fb,
    const std::vector<float>& depth,
    const DepthOfFieldSettings& dof,
    const std::optional<raster::BBox>& clip = std::nullopt)
{
    if (!dof.enabled) return;

    constexpr float kUnsetDepth = 1e18f;

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

    // Pre-compute per-pixel blur radii from the depth buffer.
    std::vector<float> blur_radii(static_cast<size_t>(w) * h, 0.0f);
    float max_r = 0.0f;
    for (i32 y = y0; y < y1; ++y) {
        for (i32 x = x0; x < x1; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            const float z = depth[idx];
            if (z < kUnsetDepth * 0.5f) {
                blur_radii[idx] = compute_dof_blur_radius(dof, z);
            }
            max_r = std::max(max_r, blur_radii[idx]);
        }
    }
    if (max_r < 0.5f) return; // No visible blur

    // Scratch buffers for the two-pass separable blur.
    // `hpass` stores the horizontally-blurred result; `output` stores the final.
    // Both are allocated at full framebuffer size (including non-clipped regions
    // which are copied unchanged from the source).
    std::vector<Color> hpass(static_cast<size_t>(w) * h);
    std::vector<Color> output(static_cast<size_t>(w) * h);

    // Copy source into hpass as the starting point (non-clipped rows stay unchanged)
    for (i32 y = 0; y < h; ++y) {
        const Color* src_row = fb.pixels_row(y);
        std::copy_n(src_row, w, &hpass[static_cast<size_t>(y) * w]);
    }

    // ── Pass 1: Horizontal blur (TBB parallel over rows) ───────────────
    // SIMD-accelerated: inner gather loop processes multiple neighbors at
    // once using Highway SIMD vectors (see dof_simd.hpp / dof_h_gather_simd).
    tbb::parallel_for(tbb::blocked_range<i32>(y0, y1),
        [&](const tbb::blocked_range<i32>& range) {
            for (i32 y = range.begin(); y < range.end(); ++y) {
                const Color* src_row = fb.pixels_row(y);
                Color* dst_row = &hpass[static_cast<size_t>(y) * w];

                dof_h_gather_simd(src_row, dst_row, blur_radii.data(),
                                  x0, x1, w, y, w);
            }
        }
    );

    // Copy hpass into output as the starting point for vertical pass
    // (non-clipped rows stay as horizontally-blurred result)
    std::copy(hpass.begin(), hpass.end(), output.begin());

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
                                      blur_radii.data(), tx0, tx1, y, h, w);
                }
            }
        }
    );

    // ── Write result back to framebuffer (parallel + SIMD) ─────────────
    tbb::parallel_for(tbb::blocked_range<i32>(y0, y1),
        [&](const tbb::blocked_range<i32>& range) {
            using namespace hwy::HWY_NAMESPACE;
            const ScalableTag<float> df;
            const size_t lanes = Lanes(df);
            const int floats_per_row = w * 4;

            for (i32 y = range.begin(); y < range.end(); ++y) {
                const float* src = reinterpret_cast<const float*>(
                    &output[static_cast<size_t>(y) * w]);
                float* dst = reinterpret_cast<float*>(fb.pixels_row(y));

                int fx = 0;
                for (; fx + static_cast<int>(lanes) <= floats_per_row; fx += static_cast<int>(lanes)) {
                    Store(Load(df, src + fx), df, dst + fx);
                }
                for (; fx < floats_per_row; ++fx) {
                    dst[fx] = src[fx];
                }
            }
        }
    );
}

} // namespace chronon3d::renderer
