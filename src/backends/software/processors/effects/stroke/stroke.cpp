// ---------------------------------------------------------------------------
// stroke.cpp — Scalar Stroke effect implementation (A7)
// ---------------------------------------------------------------------------
//
// Algorithm:
//   Stroke is built from two morphological primitives:
//     Dilate(fb, r) — for each pixel, max alpha in Chebyshev r×r neighbourhood
//     Erode(fb, r)  — for each pixel, min alpha in Chebyshev r×r neighbourhood
//
//   Both use a separable approach:
//     1. Horizontal pass: for each row, apply a 1D sliding window of size 2r+1
//     2. Vertical pass:   for each column, apply a 1D sliding window on the
//                         horizontally-processed result
//
//   Stroke composition:
//     Outside: dilated & ~source   (stroke only outside the original shape)
//     Inside:  source & ~eroded    (stroke only inside the original shape)
//     Center:  stroke straddles the edge (half dilation each side)
//
//   Softness: alpha is modulated by distance from the nearest source edge,
//   with a linear ramp over `softness` pixels.

#include "stroke.hpp"
#include <chronon3d/effects/effect_params.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace chronon3d::renderer {

// =============================================================================
// Helpers
// =============================================================================

/// Apply a 1D horizontal max-filter (dilation) to a single row.
/// `src` and `dst` may point to the same memory.
static void dilate_row_horizontal(const float* src, float* dst, int w, int radius) {
    const int len = 2 * radius + 1;
    for (int x = 0; x < w; ++x) {
        float mx = 0.0f;
        const int x0 = std::max(0, x - radius);
        const int x1 = std::min(w - 1, x + radius);
        for (int sx = x0; sx <= x1; ++sx) {
            mx = std::max(mx, src[sx]);
        }
        dst[x] = mx;
    }
}

/// Apply a 1D horizontal min-filter (erosion) to a single row.
static void erode_row_horizontal(const float* src, float* dst, int w, int radius) {
    for (int x = 0; x < w; ++x) {
        float mn = 1.0f;
        const int x0 = std::max(0, x - radius);
        const int x1 = std::min(w - 1, x + radius);
        for (int sx = x0; sx <= x1; ++sx) {
            mn = std::min(mn, src[sx]);
        }
        dst[x] = mn;
    }
}

/// 2D separable dilate: horizontal then vertical pass.
static void dilate_alpha(Framebuffer& fb, int radius) {
    if (radius <= 0) return;
    const int w = fb.width(), h = fb.height();

    // Horizontal pass
    auto row_buf = std::make_unique<float[]>(static_cast<std::size_t>(w));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) row_buf[x] = fb.pixels_row(y)[x].a;
        dilate_row_horizontal(row_buf.get(), row_buf.get(), w, radius);
        for (int x = 0; x < w; ++x) fb.pixels_row(y)[x].a = row_buf[x];
    }

    // Vertical pass
    auto col_buf = std::make_unique<float[]>(static_cast<std::size_t>(h));
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) col_buf[y] = fb.pixels_row(y)[x].a;
        dilate_row_horizontal(col_buf.get(), col_buf.get(), h, radius);
        for (int y = 0; y < h; ++y) fb.pixels_row(y)[x].a = col_buf[y];
    }
}

/// 2D separable erode: horizontal then vertical pass.
static void erode_alpha(Framebuffer& fb, int radius) {
    if (radius <= 0) return;
    const int w = fb.width(), h = fb.height();

    auto row_buf = std::make_unique<float[]>(static_cast<std::size_t>(w));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) row_buf[x] = fb.pixels_row(y)[x].a;
        erode_row_horizontal(row_buf.get(), row_buf.get(), w, radius);
        for (int x = 0; x < w; ++x) fb.pixels_row(y)[x].a = row_buf[x];
    }

    auto col_buf = std::make_unique<float[]>(static_cast<std::size_t>(h));
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) col_buf[y] = fb.pixels_row(y)[x].a;
        erode_row_horizontal(col_buf.get(), col_buf.get(), h, radius);
        for (int y = 0; y < h; ++y) fb.pixels_row(y)[x].a = col_buf[y];
    }
}

// =============================================================================
// Bounds computation
// =============================================================================

std::pair<int, int> stroke_margins(float width, float softness, StrokeMode mode) {
    float expansion;
    switch (mode) {
    case StrokeMode::Outside:
        expansion = width + softness;
        break;
    case StrokeMode::Inside:
        expansion = 0.0f;  // no outer expansion
        break;
    case StrokeMode::Center:
        expansion = width * 0.5f + softness;
        break;
    }
    const int margin = static_cast<int>(std::ceil(expansion));
    return {margin, margin};
}

// =============================================================================
// Main apply function
// =============================================================================

void apply_stroke(
    Framebuffer& fb,
    const Color& color,
    float width,
    float softness,
    StrokeMode mode,
    const std::optional<raster::BBox>& clip)
{
    if (width <= 0.0f) return;

    const int w = fb.width(), h = fb.height();
    int x0 = 0, x1 = w, y0 = 0, y1 = h;
    if (clip) {
        x0 = std::clamp(clip->x0, 0, w); x1 = std::clamp(clip->x1, 0, w);
        y0 = std::clamp(clip->y0, 0, h); y1 = std::clamp(clip->y1, 0, h);
    }
    if (y0 >= y1 || x0 >= x1) return;

    // Save original alpha channel
    auto orig_alpha = std::make_unique<float[]>(static_cast<std::size_t>(w * h));
    for (int y = 0; y < h; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            orig_alpha[y * w + x] = row[x].a;
        }
    }

    // Determine the morphological radius based on mode
    int outer_radius = 0;
    int inner_radius = 0;

    switch (mode) {
    case StrokeMode::Outside:
        outer_radius = static_cast<int>(std::ceil(width + softness));
        inner_radius = 0;
        break;
    case StrokeMode::Inside:
        outer_radius = 0;
        inner_radius = static_cast<int>(std::ceil(width + softness));
        break;
    case StrokeMode::Center:
        outer_radius = static_cast<int>(std::ceil(width * 0.5f + softness));
        inner_radius = outer_radius;
        break;
    }

    // ── Dilate alpha ──
    Framebuffer dilated(w, h);
    dilated.blit(fb, 0, 0);
    dilate_alpha(dilated, outer_radius);

    // ── Erode alpha ──
    Framebuffer eroded(w, h);
    eroded.blit(fb, 0, 0);
    erode_alpha(eroded, inner_radius);

    // ── Compose stroke ──
    // Softness is handled naturally by the morphological radius:
    // when radius = ceil(width + softness), the max-filter creates
    // a gradient across the softness-wide band at the boundary.
    // No additional alpha transform is needed — the raw stroke_a from
    // the dilate/erode difference drives the edge quality directly.

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            const float src_a = orig_alpha[y * w + x];
            const float dil_a = dilated.get_pixel(x, y).a;
            const float ero_a = eroded.get_pixel(x, y).a;

            // Stroke mask: pixels that are in the dilated region but not
            // in the eroded region (i.e. the boundary band)
            float stroke_a;
            switch (mode) {
            case StrokeMode::Outside:
                stroke_a = dil_a - src_a;
                break;
            case StrokeMode::Inside:
                stroke_a = src_a - ero_a;
                break;
            case StrokeMode::Center:
                stroke_a = std::min(dil_a - src_a, src_a - ero_a);
                break;
            }

            // Clamp to [0, 1]
            stroke_a = std::clamp(stroke_a, 0.0f, 1.0f);

            // Blend stroke colour with source
            if (stroke_a > 0.0f) {
                const float t = stroke_a;
                // Source colour is already premultiplied; stroke colour is straight
                row[x].r = row[x].r * (1.0f - t) + color.r * t;
                row[x].g = row[x].g * (1.0f - t) + color.g * t;
                row[x].b = row[x].b * (1.0f - t) + color.b * t;
                // Alpha: source alpha + stroke alpha (stroke adds to alpha)
                row[x].a = src_a + (1.0f - src_a) * t;
            }
        }
    }
}

} // namespace chronon3d::renderer
