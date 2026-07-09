#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <algorithm>
#include <utility>

namespace chronon3d::test::completeness {

// ── Pixel scanning helpers for text completeness tests ─────────────────
// Shared across VisibleInk, Wrapping, Alignment, Unicode, and
// Determinism tests.  All functions take a Framebuffer and an alpha
// epsilon threshold (default 0.01f).

/// Count pixels with alpha above epsilon (real ink coverage).
inline int count_visible_pixels(const Framebuffer& fb,
                                float alpha_epsilon = 0.01f) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            if (row[x].a > alpha_epsilon) {
                ++count;
            }
        }
    }
    return count;
}

/// Count pixels with luminance above a threshold (visible bright ink on dark bg).
inline int count_bright_pixels(const Framebuffer& fb,
                               float luma_threshold = 0.05f) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            float luma = 0.2126f * row[x].r + 0.7152f * row[x].g + 0.0722f * row[x].b;
            if (luma > luma_threshold) {
                ++count;
            }
        }
    }
    return count;
}

/// Mean alpha across the entire framebuffer.
inline float mean_alpha(const Framebuffer& fb) {
    double sum = 0.0;
    const int total = fb.width() * fb.height();
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            sum += row[x].a;
        }
    }
    return static_cast<float>(sum / total);
}

/// Count distinct rows that contain at least one visible pixel.
/// Higher count → more wrapped lines.
inline int count_ink_rows(const Framebuffer& fb,
                          float alpha_epsilon = 0.01f) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            if (row[x].a > alpha_epsilon) {
                ++count;
                break;  // one pixel per row is enough
            }
        }
    }
    return count;
}

/// Find the vertical extent of ink: first and last row with visible pixels.
/// Returns {top_row, bottom_row} or {-1, -1} if no ink.
inline std::pair<int, int> ink_vertical_extent(
    const Framebuffer& fb, float alpha_epsilon = 0.01f) {
    int top = -1, bottom = -1;
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            if (row[x].a > alpha_epsilon) {
                if (top < 0) top = y;
                bottom = y;
                break;
            }
        }
    }
    return {top, bottom};
}

/// Find the maximum horizontal extent of ink in any single row.
/// Used to verify no row exceeds the box width.
inline int max_ink_row_width(const Framebuffer& fb,
                             float alpha_epsilon = 0.01f) {
    int max_width = 0;
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        int first = -1, last = -1;
        for (int x = 0; x < fb.width(); ++x) {
            if (row[x].a > alpha_epsilon) {
                if (first < 0) first = x;
                last = x;
            }
        }
        if (first >= 0) {
            max_width = std::max(max_width, last - first + 1);
        }
    }
    return max_width;
}

} // namespace chronon3d::test::completeness
