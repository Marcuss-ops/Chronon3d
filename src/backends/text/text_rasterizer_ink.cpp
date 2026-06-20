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
/// Returns ink_center_frac (fractional X center) and sets out parameters.
/// Also optionally trims trailing transparent rows.
float compute_text_ink_bbox(
    BLImage& img,
    int padding,
    bool box_enabled,
    float font_size,
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
    auto* pixels = reinterpret_cast<uint32_t*>(trim_data.pixelData);

    constexpr int kSampleStrideX = 4;
    constexpr int kSampleStrideY = 2;
    std::vector<int> row_counts(static_cast<size_t>(sh), 0);
    int max_count = 0;
    int local_ink_left = sw, local_ink_right = 0;
    int local_ink_top = sh, local_ink_bottom = 0;
    for (int y = 0; y < sh; y += kSampleStrideY) {
        for (int x = 0; x < sw; x += kSampleStrideX) {
            if (((pixels[y * stride + x] >> 24) & 0xFF) != 0) {
                ++row_counts[static_cast<size_t>(y)];
                if (x < local_ink_left)   local_ink_left  = x;
                if (x > local_ink_right)  local_ink_right = x;
                if (y < local_ink_top)    local_ink_top    = y;
                if (y > local_ink_bottom) local_ink_bottom = y;
            }
        }
        if (row_counts[static_cast<size_t>(y)] > max_count)
            max_count = row_counts[static_cast<size_t>(y)];
    }
    local_ink_left   = std::max(0, local_ink_left - (kSampleStrideX - 1));
    local_ink_right  = std::min(sw - 1, local_ink_right + kSampleStrideX - 1);
    local_ink_top    = std::max(0, local_ink_top - (kSampleStrideY - 1));
    local_ink_bottom = std::min(sh - 1, local_ink_bottom + kSampleStrideY - 1);
    for (int y = 1; y < sh; ++y) {
        if (row_counts[static_cast<size_t>(y)] == 0 &&
            y % kSampleStrideY != 0 &&
            row_counts[static_cast<size_t>(y - 1)] > 0) {
            row_counts[static_cast<size_t>(y)] =
                row_counts[static_cast<size_t>(y - 1)];
        }
    }

    if (max_count > 0) {
        const int content_threshold = std::max(5,
            static_cast<int>(std::ceil(static_cast<float>(max_count) * 0.02f)));
        int last_content_row = -1;
        for (int y = 0; y < sh; ++y) {
            if (row_counts[static_cast<size_t>(y)] > content_threshold) {
                last_content_row = y;
            }
        }

        if (!box_enabled) {
            const int descender_margin = std::max(8,
                static_cast<int>(std::ceil(font_size * 0.25f)));
            const int safe_bottom = std::max(last_content_row,
                local_ink_bottom + descender_margin);
            if (safe_bottom >= 0 && safe_bottom < sh - 1) {
                for (int y = safe_bottom + 1; y < sh; ++y) {
                    std::fill_n(pixels + y * stride, sw, uint32_t{0});
                }
            }

            const int trace_threshold = std::max(2,
                static_cast<int>(std::ceil(static_cast<float>(max_count) * 0.005f)));
            for (int y = sh - 1; y >= 0; --y) {
                const int count = row_counts[static_cast<size_t>(y)];
                if (count == 0) continue;
                if (count > trace_threshold && y <= safe_bottom) break;
                std::fill_n(pixels + y * stride, sw, uint32_t{0});
            }
        }

        ink_left  = local_ink_left;
        ink_right = local_ink_right;
        ink_top   = local_ink_top;
        ink_bottom = local_ink_bottom;
        if (ink_left < ink_right) {
            const float ink_cx = 0.5f * static_cast<float>(ink_left + ink_right);
            ink_center_frac = ink_cx;
            ink_w = static_cast<float>(ink_right - ink_left);
        }
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
