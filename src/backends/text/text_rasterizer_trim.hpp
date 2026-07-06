// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// FASE 3 Step 3 — Pixel-ink trimming extracted from text_rasterizer_render.cpp
//
// trim_and_compute_ink_bounds(): trims trailing empty/alpha rows from the
// rendered image and computes the ink bounding box for centering.
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <blend2d.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace chronon3d {
namespace detail {

/// Result of pixel-ink trimming.
struct InkTrimResult {
    float ink_center_frac = -1.0f;
    float ink_w           = 0.0f;
    int   ink_left        = 0;
    int   ink_right       = 0;
    int   ink_top          = 0;
    int   ink_bottom       = 0;
};

/// Trim trailing empty rows from the rendered image and compute the
/// ink bounding box.  When `!box_enabled`, trailing descender/antialiasing
/// artifacts are zeroed; the ink bounds are computed for centering use.
inline InkTrimResult trim_and_compute_ink_bounds(
    BLImage& img,
    float font_size,
    bool box_enabled)
{
    InkTrimResult out;

    BLImageData trim_data;
    if (img.getData(&trim_data) != BL_SUCCESS || !trim_data.pixelData || trim_data.size.h <= 0)
        return out;

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

        out.ink_left   = local_ink_left;
        out.ink_right  = local_ink_right;
        out.ink_top    = local_ink_top;
        out.ink_bottom = local_ink_bottom;
        if (out.ink_left < out.ink_right) {
            const float ink_cx = 0.5f * static_cast<float>(out.ink_left + out.ink_right);
            out.ink_center_frac = ink_cx;
            out.ink_w = static_cast<float>(out.ink_right - out.ink_left);
        }
    }

    return out;
}

} // namespace detail
} // namespace chronon3d
