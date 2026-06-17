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

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>
#include <atomic>
#include <span>

namespace chronon3d {
namespace simd {

/// Premultiplied alpha "over" composite (SRC_OVER).
///
/// For each pixel i:
///   dst[i].rgb = src[i].rgb + dst[i].rgb * (1 - src[i].a)
///   dst[i].a   = src[i].a   + dst[i].a   * (1 - src[i].a)
///
/// `dst` and `src` must have the same size.
/// `force_scalar` — when true, use the safe scalar fallback instead of Highway SIMD
/// (useful for diagnosing SIMD-related rendering regressions).
void composite_normal_premul(std::span<Color> dst,
                              std::span<const Color> src,
                              bool force_scalar = false);

/// Add blend: dst[i] += src[i]  (per component).
void composite_add_premul(std::span<Color> dst,
                           std::span<const Color> src);

/// Multiply blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   dst[i].rgb *= src[i].rgb
void composite_multiply_premul(std::span<Color> dst,
                                std::span<const Color> src);

/// Screen blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   dst[i].rgb = src[i].rgb + dst[i].rgb - src[i].rgb * dst[i].rgb
void composite_screen_premul(std::span<Color> dst,
                              std::span<const Color> src);

/// Overlay blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   For each channel c: if dst.c < 0.5: 2*src.c*dst.c  else 1-2*(1-src.c)*(1-dst.c)
void composite_overlay_premul(std::span<Color> dst,
                               std::span<const Color> src);

/// Darken blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   dst[i].rgb = min(src[i].rgb, dst[i].rgb)
void composite_darken_premul(std::span<Color> dst,
                              std::span<const Color> src);

/// Lighten blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   dst[i].rgb = max(src[i].rgb, dst[i].rgb)
void composite_lighten_premul(std::span<Color> dst,
                               std::span<const Color> src);

/// Difference blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   dst[i].rgb = abs(src[i].rgb - dst[i].rgb)
void composite_difference_premul(std::span<Color> dst,
                                  std::span<const Color> src);

/// Exclusion blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   dst[i].rgb = src.rgb + dst.rgb - 2 * src.rgb * dst.rgb
void composite_exclusion_premul(std::span<Color> dst,
                                 std::span<const Color> src);

/// SoftLight blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   Uses the canonical soft_light formula with soft_light_d() helper.
///   Input straight RGB clamped to [0,1] per HDR contract.
void composite_soft_light_premul(std::span<Color> dst,
                                  std::span<const Color> src);

/// HardLight blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   if cs <= 0.5: 2*cb*cs  else 1-2*(1-cb)*(1-cs)
///   Input straight RGB clamped to [0,1] per HDR contract.
void composite_hard_light_premul(std::span<Color> dst,
                                  std::span<const Color> src);

/// ColorDodge blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   cs >= 1 → 1,  cb <= 0 → 0,  else min(1, cb/(1-cs))
///   Input straight RGB clamped to [0,1] per HDR contract.
void composite_color_dodge_premul(std::span<Color> dst,
                                   std::span<const Color> src);

/// ColorBurn blend:
///   dst[i].a = src[i].a + dst[i].a * (1 - src[i].a)
///   cs <= 0 → 0,  cb >= 1 → 1,  else 1 - min(1, (1-cb)/cs)
///   Input straight RGB clamped to [0,1] per HDR contract.
void composite_color_burn_premul(std::span<Color> dst,
                                  std::span<const Color> src);

/// Fill contiguous Color elements with `color`.
void clear_framebuffer(std::span<Color> data, const Color& color);

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

/// Vectorized alpha premultiplication for RGBA8 to PRGB32 (Blend2D format).
/// `src` is RGBA8, `dst` is PRGB32 (0xAARRGGBB).
void premultiply_alpha_rgba8(uint32_t* __restrict__ dst, const uint8_t* __restrict__ src, int pixel_count);

/// Convert a row of PRGB32 pixels (premultiplied 0xAARRGGBB) to Color (float RGBA, un-premultiplied).
/// Processes `pixel_count` contiguous pixels.  `dst` must point to `pixel_count` contiguous Color elements.
/// Caller is responsible for stride offset (pass src = base + y * stride).
void bl_image_prgb32_to_color_row(Color* __restrict__ dst,
                                   const uint32_t* __restrict__ src,
                                   int pixel_count);

/// Convert a row of Color (float RGBA) to PRGB32 pixels (premultiplied 0xAARRGGBB).
/// Processes `pixel_count` contiguous pixels.  `src` must point to `pixel_count` contiguous Color elements.
/// Caller is responsible for stride offset (pass dst = base + y * stride).
void color_to_prgb32_row(uint32_t* __restrict__ dst,
                          const Color* __restrict__ src,
                          int pixel_count);

/// Apply alpha matte coverage to a contiguous run of target pixels.
/// Each target pixel[i] is multiplied by matte[i].alpha (or 1-alpha if inverted).
void apply_alpha_matte_premul(
    std::span<Color> target,
    std::span<const Color> matte,
    bool inverted
);

/// Apply luma matte coverage to a contiguous run of target pixels.
/// Each target pixel[i] is multiplied by matte[i].luma (or 1-luma if inverted).
void apply_luma_matte_premul(
    std::span<Color> target,
    std::span<const Color> matte,
    bool inverted
);

} // namespace simd
} // namespace chronon3d
