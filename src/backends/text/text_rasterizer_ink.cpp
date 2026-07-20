// ── Text rasterization ink-bbox computation ───────────────────────────────
// Extracted from rasterize_text_to_bl_image to keep the render function
// at a manageable size.

#include <blend2d.h>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace chronon3d {

/// Compute ink-bounds from a rendered BLImage.
///
/// Performs a deterministic full-frame alpha scan (no sub-sampling) and
/// never mutates the input image. The result is intended for audit /
/// post-render reconciliation only; placement must use the geometric
/// outline-based bbox computed by the render graph (see
/// compute_text_run_visual_bounds / compute_text_run_world_bbox).
///
/// Returns ink_center_frac (fractional X center) and sets out parameters.
/// On failure / no visible ink returns -1.0f and zeroed out params.
float compute_text_ink_bbox(
    BLImage& img,
    [[maybe_unused]] int padding,
    [[maybe_unused]] bool box_enabled,
    [[maybe_unused]] float font_size,
    int& out_ink_left,
    int& out_ink_right,
    int& out_ink_top,
    int& out_ink_bottom,
    float& out_ink_w)
{
    float ink_center_frac = -1.0f;
    float ink_w = 0.0f;
    int ink_left = 0, ink_right = 0, ink_top = 0, ink_bottom = 0;

    BLImageData trim_data;
    if (img.getData(&trim_data) != BL_SUCCESS || !trim_data.pixelData || trim_data.size.h <= 0) {
        out_ink_left = 0; out_ink_right = 0; out_ink_top = 0; out_ink_bottom = 0;
        out_ink_w = 0.0f;
        return -1.0f;
    }

    const int sw = trim_data.size.w;
    const int sh = trim_data.size.h;
    const int stride = static_cast<int>(trim_data.stride / sizeof(uint32_t));
    const uint32_t* pixels = static_cast<const uint32_t*>(trim_data.pixelData);

    int local_ink_left = sw;
    int local_ink_right = -1;
    int local_ink_top = sh;
    int local_ink_bottom = -1;

    for (int y = 0; y < sh; ++y) {
        bool row_has_ink = false;
        const uint32_t* row = pixels + y * stride;
        for (int x = 0; x < sw; ++x) {
            // Use the same threshold as the canonical alpha_bbox_scanner so
            // the post-render audit agrees with the geometric pipeline.
            if ((row[x] >> 24) & 0xFF) {
                row_has_ink = true;
                if (x < local_ink_left)   local_ink_left  = x;
                if (x > local_ink_right)  local_ink_right = x;
                if (y < local_ink_top)    local_ink_top    = y;
                if (y > local_ink_bottom) local_ink_bottom = y;
            }
        }
        if (row_has_ink) {
            if (y < local_ink_top)    local_ink_top    = y;
            if (y > local_ink_bottom) local_ink_bottom = y;
        }
    }

    if (local_ink_left <= local_ink_right && local_ink_top <= local_ink_bottom) {
        ink_left  = local_ink_left;
        ink_right = local_ink_right;
        ink_top   = local_ink_top;
        ink_bottom = local_ink_bottom;
        const float ink_cx = 0.5f * static_cast<float>(ink_left + ink_right);
        ink_center_frac = ink_cx;
        ink_w = static_cast<float>(ink_right - ink_left + 1);
    }

    out_ink_left = ink_left;
    out_ink_right = ink_right;
    out_ink_top = ink_top;
    out_ink_bottom = ink_bottom;
    out_ink_w = ink_w;
    return ink_center_frac;
}

/// Draw debug bounding-box overlays on the rendered text image.
/// Controlled by detail::g_debug_config->text_bbox().
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
    float baseline_y)
{
    BLContext dbg(img);
    dbg.setCompOp(BL_COMP_OP_SRC_OVER);

    const float pad_f = static_cast<float>(padding) * 0.5f;

    if (box_enabled) {
        dbg.setStrokeStyle(BLRgba32(255, 48, 48, 200));
        dbg.setStrokeWidth(2.0f);
        dbg.strokeRect(pad_f, pad_f, box_w, box_h);

        const float cx = pad_f + box_w * 0.5f;
        const float cy = pad_f + box_h * 0.5f;
        dbg.setStrokeStyle(BLRgba32(0, 255, 255, 200));
        dbg.setStrokeWidth(1.0f);
        dbg.strokeLine(cx - 14.0f, cy, cx + 14.0f, cy);
        dbg.strokeLine(cx, cy - 14.0f, cx, cy + 14.0f);
    }

    if (ink_left < ink_right) {
        const float ink_x = static_cast<float>(ink_left);
        const float ink_y = static_cast<float>(ink_top);
        const float ink_w2 = static_cast<float>(ink_right - ink_left);
        const float ink_h = static_cast<float>(ink_bottom - ink_top);
        dbg.setStrokeStyle(BLRgba32(48, 255, 48, 200));
        dbg.setStrokeWidth(1.0f);
        dbg.strokeRect(ink_x, ink_y, ink_w2, ink_h);

        const float icx = static_cast<float>(ink_left + ink_right) * 0.5f;
        const float icy = static_cast<float>(ink_top + ink_bottom) * 0.5f;
        dbg.setStrokeStyle(BLRgba32(255, 255, 48, 200));
        dbg.setStrokeWidth(1.0f);
        dbg.strokeLine(icx - 8.0f, icy, icx + 8.0f, icy);
        dbg.strokeLine(icx, icy - 8.0f, icx, icy + 8.0f);
    }

    dbg.setStrokeStyle(BLRgba32(48, 128, 255, 180));
    dbg.setStrokeWidth(1.0f);
    dbg.strokeLine(0.0f, baseline_y, static_cast<float>(img_w), baseline_y);

    dbg.end();
}

} // namespace chronon3d
