#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/text_clip/test_helpers.hpp
//
// Shared pixel-scanning utilities for text clip / completeness golden tests.
//
// Provides:
//   - AlphaBBox         — tight axis-aligned bbox of visible pixels
//   - alpha_bbox()      — scan Framebuffer for alpha bbox
//   - alpha_pixel_count() — count visible pixels
//   - alpha_centroid()  — alpha-weighted centroid of visible pixels
//   - alpha_touches_edge() — edge-touch detection for clipping regression
//
// Used by: text_clip_bounds.cpp, text_completeness.cpp
// Keep this header self-contained (no project-internal includes beyond
// Framebuffer + Color).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::test {

// ── AlphaBBox ──────────────────────────────────────────────────────────
// Tight axis-aligned bounding box of all pixels with alpha > epsilon.
// Returns (-1,-1,-1,-1) when the framebuffer contains no opaque draw.
struct AlphaBBox {
    int x0{-1}, y0{-1}, x1{-1}, y1{-1};

    [[nodiscard]] int width()  const noexcept {
        return (x1 >= 0) ? (x1 - x0 + 1) : 0;
    }
    [[nodiscard]] int height() const noexcept {
        return (y1 >= 0) ? (y1 - y0 + 1) : 0;
    }
    [[nodiscard]] bool empty() const noexcept {
        return x1 < 0 || y1 < 0;
    }

    // ── Edge-touch queries ─────────────────────────────────────────────
    [[nodiscard]] bool touches_left(int margin = 4) const noexcept {
        return x0 >= 0 && x0 <= margin;
    }
    [[nodiscard]] bool touches_top(int margin = 4) const noexcept {
        return y0 >= 0 && y0 <= margin;
    }
    [[nodiscard]] bool touches_right(int canvas_w, int margin = 4) const noexcept {
        return x1 >= 0 && x1 >= canvas_w - margin;
    }
    [[nodiscard]] bool touches_bottom(int canvas_h, int margin = 4) const noexcept {
        return y1 >= 0 && y1 >= canvas_h - margin;
    }
};

// ── alpha_bbox ─────────────────────────────────────────────────────────
// Scan the framebuffer for the tight bounding box of all pixels with
// alpha > epsilon.  Returns {-1,-1,-1,-1} if no visible pixels found.
[[nodiscard]] inline AlphaBBox alpha_bbox(
    const Framebuffer& fb,
    float epsilon = 0.01f
) {
    AlphaBBox b{fb.width(), fb.height(), -1, -1};
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            if (row[x].a > epsilon) {
                b.x0 = std::min(b.x0, x);
                b.x1 = std::max(b.x1, x);
                b.y0 = std::min(b.y0, y);
                b.y1 = std::max(b.y1, y);
            }
        }
    }
    return b;
}

// ── alpha_pixel_count ──────────────────────────────────────────────────
// Count the number of pixels with alpha > epsilon.
[[nodiscard]] inline int alpha_pixel_count(
    const Framebuffer& fb,
    float epsilon = 0.01f
) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < fb.width(); ++x) {
            if (row[x].a > epsilon) ++count;
        }
    }
    return count;
}

// ── AlphaCentroid ──────────────────────────────────────────────────────
// Alpha-weighted centroid of visible pixels.
struct AlphaCentroid {
    float x{0.0f};
    float y{0.0f};
    float max_alpha{0.0f};

    [[nodiscard]] bool on_canvas(int canvas_w, int canvas_h) const noexcept {
        return x > 0.0f && y > 0.0f &&
               x < static_cast<float>(canvas_w) &&
               y < static_cast<float>(canvas_h);
    }
};

// ── alpha_centroid ─────────────────────────────────────────────────────
// Compute the alpha-weighted centroid of visible pixels.
// Returns {-1,-1,0} if no visible pixels found.
[[nodiscard]] inline AlphaCentroid alpha_centroid(
    const Framebuffer& fb,
    float alpha_threshold = 0.05f
) {
    AlphaCentroid c;
    double sum_x = 0.0, sum_y = 0.0, sum_w = 0.0;
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());

    for (int y = 0; y < h; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            const float a = row[x].a;
            if (a > c.max_alpha) c.max_alpha = a;
            if (a > alpha_threshold) {
                sum_x += static_cast<double>(x) * static_cast<double>(a);
                sum_y += static_cast<double>(y) * static_cast<double>(a);
                sum_w += static_cast<double>(a);
            }
        }
    }

    if (sum_w > 0.0) {
        c.x = static_cast<float>(sum_x / sum_w);
        c.y = static_cast<float>(sum_y / sum_w);
    } else {
        c.x = -1.0f;
        c.y = -1.0f;
    }
    return c;
}

// ── alpha_touches_edge ─────────────────────────────────────────────────
// Returns true if any pixel with alpha > threshold touches any edge
// of the framebuffer.  This indicates potential clipping: the internal
// raster surface was too small.
[[nodiscard]] inline bool alpha_touches_edge(
    const Framebuffer& fb,
    float alpha_threshold = 0.05f
) {
    const int w = static_cast<int>(fb.width());
    const int h = static_cast<int>(fb.height());

    // Top / bottom row
    const Color* top = fb.pixels_row(0);
    const Color* bot = fb.pixels_row(h - 1);
    for (int x = 0; x < w; ++x) {
        if (top[x].a > alpha_threshold) return true;
        if (bot[x].a > alpha_threshold) return true;
    }
    // Left / right column
    for (int y = 0; y < h; ++y) {
        const Color* row = fb.pixels_row(y);
        if (row[0].a > alpha_threshold) return true;
        if (row[w - 1].a > alpha_threshold) return true;
    }
    return false;
}

} // namespace chronon3d::test
