#pragma once

#include <blend2d.h>

namespace chronon3d {

/// Compute ink-bounds from a rendered BLImage.
///
/// Performs a deterministic full-frame alpha scan. The image is never
/// modified. The returned bounds are intended for audit / post-render
/// reconciliation only; placement decisions must use the geometric
/// outline-based bbox from the render graph.
///
/// Returns the fractional X-center of the ink bbox (or -1.0f if no ink found).
float compute_text_ink_bbox(
    BLImage& img,
    int padding,
    bool box_enabled,
    float font_size,
    int& out_ink_left,
    int& out_ink_right,
    int& out_ink_top,
    int& out_ink_bottom,
    float& out_ink_w);

/// Draw debug bounding-box overlays on the rendered text image.
/// Controlled by detail::g_debug_config->text_bbox() — caller gates the call.
void draw_text_debug_overlays(
    BLImage& img,
    int img_w,
    int img_h,
    int padding,
    bool box_enabled,
    float box_w,
    float box_h,
    int ink_left,
    int ink_right,
    int ink_top,
    int ink_bottom,
    float baseline_y);

} // namespace chronon3d
