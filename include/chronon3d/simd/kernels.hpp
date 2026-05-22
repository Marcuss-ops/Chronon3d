#pragma once

// ---------------------------------------------------------------------------
// Portable SIMD kernels using Google Highway.
//
// These kernels operate on contiguous float[4] pixel data (Color arrays).
// Highway dispatches to the best available SIMD target at runtime
// (AVX2, SSE4, ARM NEON, etc.), with a scalar fallback.
//
// Architecture:
//   Public API   →  include/chronon3d/simd/kernels.hpp
//   Highway impl →  src/backends/software/simd/highway_kernels.cpp
//   Scalar impl  →  src/backends/software/simd/scalar_kernels.cpp
// ---------------------------------------------------------------------------

#include <chronon3d/core/types.hpp>
#include <chronon3d/math/color.hpp>

namespace chronon3d {
namespace simd {

/// Premultiplied alpha "over" composite (SRC_OVER).
///
/// For each pixel i:
///   dst[i].rgb = src[i].rgb + dst[i].rgb * (1 - src[i].a)
///   dst[i].a   = src[i].a   + dst[i].a   * (1 - src[i].a)
///
/// `pixel_count` is the number of Color (4×f32) elements to process.
void composite_normal_premul(Color* __restrict__ dst,
                              const Color* __restrict__ src,
                              int pixel_count);

/// Fill `pixel_count` contiguous Color elements with `color`.
void clear_framebuffer(Color* data, int pixel_count, const Color& color);

/// Vectorized rasterization for transformed rectangles.
void rasterize_rect_simd(
    Color* __restrict__ row,
    const float* __restrict__ lp_h_start,
    const float* __restrict__ col0,
    int pixel_count,
    float rect_w, float rect_h,
    float spread,
    const Color& color
);

} // namespace simd
} // namespace chronon3d
