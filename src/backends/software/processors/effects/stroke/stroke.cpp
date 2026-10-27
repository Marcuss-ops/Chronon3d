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
#include <chronon3d/backends/software/software_session_resources.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace chronon3d::renderer {

// =============================================================================
// Helpers
// =============================================================================

namespace {

// Adjacent alphas live in the same 64 B cache line (Color == 4 x f32), so the
// vertical morphology pass works on tiles of this many columns at once.
constexpr int kMorphTileCols = 4;

enum class MorphOp { Dilate, Erode };

/// Exact 1D sliding min/max over the window [x - radius, x + radius] —
/// van Herk–Gil–Werman. Interior positions resolve the window through the
/// block identity `extreme([x-r, x+r]) = extreme(suffix_in_block(x-r),
/// prefix_in_block(x+r))` over blocks of 2r+1 elements: O(1) per element
/// regardless of radius, versus O(radius) for a naive window. The 2*radius
/// edge positions keep the exact legacy semantics through a small clamped
/// window (dilate floors at 0; erode sees the transparent outside and floors
/// at 0 from above). Extrema are selections, not accumulations, so every
/// output value is bit-identical to the previous naive loops.
///
/// `work` must hold 2n floats; `src` and `dst` must not alias.
void morph_row_1d(const float* src, float* dst, int n, int radius,
                  MorphOp op, float* work) {
    if (n <= 0) return;
    const bool dilate = (op == MorphOp::Dilate);
    const int k = 2 * radius + 1;
    float* const prefix = work;      // extreme over [block_start(i), i]
    float* const suffix = work + n;  // extreme over [i, block_end(i)]

    for (int i = 0; i < n; ++i) {
        prefix[i] = (i % k == 0)
            ? src[i]
            : (dilate ? std::max(prefix[i - 1], src[i])
                      : std::min(prefix[i - 1], src[i]));
    }
    for (int i = n - 1; i >= 0; --i) {
        suffix[i] = ((i + 1) % k == 0 || i == n - 1)
            ? src[i]
            : (dilate ? std::max(suffix[i + 1], src[i])
                      : std::min(suffix[i + 1], src[i]));
    }

    for (int x = 0; x < n; ++x) {
        float value;
        if (x - radius < 0 || x + radius >= n) {
            const int x0 = std::max(0, x - radius);
            const int x1 = std::min(n - 1, x + radius);
            value = src[x0];
            for (int sx = x0 + 1; sx <= x1; ++sx) {
                value = dilate ? std::max(value, src[sx])
                               : std::min(value, src[sx]);
            }
            value = dilate ? std::max(0.0f, value) : std::min(0.0f, value);
        } else {
            value = dilate ? std::max(suffix[x - radius], prefix[x + radius])
                           : std::min(suffix[x - radius], prefix[x + radius]);
            value = dilate ? std::max(0.0f, value) : std::min(1.0f, value);
        }
        dst[x] = value;
    }
}

/// Separable alpha morphology (dilate/erode): horizontal pass over rows, then
/// vertical pass over tiles of kMorphTileCols adjacent columns so one cache
/// line serves every alpha of the tile instead of one line per value.
void morph_alpha(Framebuffer& fb, int radius, MorphOp op,
                 ::chronon3d::EffectScratchResources& scratch) {
    if (radius <= 0) return;
    const int w = fb.width(), h = fb.height();
    const int max_dim = std::max(w, h);
    auto& tile_in = scratch.morph_tile_in;
    auto& tile_out = scratch.morph_tile_out;
    auto& work = scratch.morph_work;
    tile_in.resize(static_cast<std::size_t>(kMorphTileCols * max_dim));
    tile_out.resize(static_cast<std::size_t>(kMorphTileCols * max_dim));
    work.resize(static_cast<std::size_t>(2 * max_dim));

    for (int y = 0; y < h; ++y) {
        Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) tile_in[static_cast<std::size_t>(x)] = row[x].a;
        morph_row_1d(tile_in.data(), tile_out.data(), w, radius, op, work.data());
        for (int x = 0; x < w; ++x) row[x].a = tile_out[static_cast<std::size_t>(x)];
    }

    for (int xb = 0; xb < w; xb += kMorphTileCols) {
        const int cols = std::min(kMorphTileCols, w - xb);
        for (int y = 0; y < h; ++y) {
            const Color* row = fb.pixels_row(y);
            for (int c = 0; c < cols; ++c) {
                tile_in[static_cast<std::size_t>(c * h + y)] = row[xb + c].a;
            }
        }
        for (int c = 0; c < cols; ++c) {
            morph_row_1d(tile_in.data() + static_cast<std::size_t>(c * h),
                         tile_out.data() + static_cast<std::size_t>(c * h),
                         h, radius, op, work.data());
        }
        for (int y = 0; y < h; ++y) {
            Color* row = fb.pixels_row(y);
            for (int c = 0; c < cols; ++c) {
                row[xb + c].a = tile_out[static_cast<std::size_t>(c * h + y)];
            }
        }
    }
}

} // namespace

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
    ::chronon3d::EffectScratchResources& scratch,
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

    // Save original alpha channel (scratch-owned, required: no heap fallback)
    scratch.ensure_size(w, h);
    auto& orig_alpha = scratch.original_alpha;
    orig_alpha.resize(static_cast<std::size_t>(w * h));
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
    Framebuffer* dilated = scratch.framebuffer.get();
    Framebuffer* eroded = scratch.framebuffer_b.get();
    dilated->blit(fb, 0, 0);
    morph_alpha(*dilated, outer_radius, MorphOp::Dilate, scratch);

    // ── Erode alpha ──
    eroded->blit(fb, 0, 0);
    morph_alpha(*eroded, inner_radius, MorphOp::Erode, scratch);

    // ── Compose stroke ──
    // Softness is handled naturally by the morphological radius:
    // when radius = ceil(width + softness), the max-filter creates
    // a gradient across the softness-wide band at the boundary.
    // No additional alpha transform is needed — the raw stroke_a from
    // the dilate/erode difference drives the edge quality directly.

    for (int y = y0; y < y1; ++y) {
        Color* row = fb.pixels_row(y);
        const Color* dil_row = dilated->pixels_row(y);
        const Color* ero_row = eroded->pixels_row(y);
        for (int x = x0; x < x1; ++x) {
            const float src_a = orig_alpha[y * w + x];
            const float dil_a = dil_row[x].a;
            const float ero_a = ero_row[x].a;

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

            if (mode == StrokeMode::Inside) {
                row[x].r = color.r * stroke_a;
                row[x].g = color.g * stroke_a;
                row[x].b = color.b * stroke_a;
                row[x].a = stroke_a;
            } else if (stroke_a > 0.0f) {
                const float t = stroke_a;
                // Source colour is already premultiplied; stroke colour is straight.
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
