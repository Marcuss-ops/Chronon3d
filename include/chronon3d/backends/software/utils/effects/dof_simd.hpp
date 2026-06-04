#pragma once

// ============================================================================
// dof_simd.hpp — SIMD-accelerated gather helpers for per-pixel DOF blur
//
// These functions vectorize the inner gather loops of the separable two-pass
// variable-radius DOF blur using Google Highway for multi-target SIMD dispatch.
// ============================================================================

#include <chronon3d/math/color.hpp>

namespace chronon3d::renderer {

/// SIMD horizontal gather: for a row of pixels, apply 1D smoothstep-weighted
/// blur along the row.  Each output pixel gathers from its own [x-r, x+r]
/// neighborhood using SIMD-batched neighbor processing.
void dof_h_gather_simd(const Color* __restrict src_row,
                       Color* __restrict dst_row,
                       const float* __restrict blur_radii_row,
                       int x0, int x1, int w, int y, int fb_w);

/// SIMD vertical gather: for a row of pixels, gather from columns within
/// [y-r, y+r] in the horizontally-blurred buffer.  Neighbors are strided
/// by row width; RGBA values are gathered into SIMD vectors before computing
/// weights.
void dof_v_gather_simd(const Color* __restrict hpass,
                       Color* __restrict output,
                       const float* __restrict blur_radii,
                       int x0, int x1, int y, int h, int fb_w);

}  // namespace chronon3d::renderer
