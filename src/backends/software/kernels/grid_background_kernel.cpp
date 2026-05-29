#include "grid_background_kernel.hpp"

#include <chronon3d/compositor/alpha.hpp>
#include <chronon3d/math/transform.hpp>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace chronon3d::renderer {

namespace {

// ── Helpers (inlined for performance) ──────────────────────────────────────

inline f32 line_weight(f32 distance, f32 thickness) noexcept {
    const f32 half = std::max(thickness * 0.5f, 0.0f);
    const f32 feather = 0.85f;
    const f32 edge0 = half + feather;
    const f32 edge1 = half - feather;
    if (distance <= edge1) return 1.0f;
    if (distance >= edge0) return 0.0f;
    return 1.0f - (distance - edge1) / std::max(edge0 - edge1, 1e-6f);
}

inline f32 cell_distance(f32 value, f32 step) noexcept {
    if (step <= 1e-6f) return 0.0f;
    const f32 cell = std::round(value / step) * step;
    return std::abs(value - cell);
}

/// Precomputed column-weight for a single x coordinate.
struct ColumnWeight {
    f32 minor{0.0f};
    f32 major{0.0f};
    bool active{false};
};

} // anonymous namespace

// ── Public entry point ──────────────────────────────────────────────────────

void render_grid_background_kernel(
    Framebuffer& fb,
    const GridBackgroundShape& grid,
    const raster::BBox& clip,
    const RenderState& state) {

    if (grid.size.x <= 0.0f || grid.size.y <= 0.0f) return;

    // ── Colour setup ──────────────────────────────────────────────────────
    const Color bg      = grid.bg_color.to_linear();
    const Color minor   = grid.grid_color.to_linear();
    Color major         = minor;
    Color bg_adj        = bg;
    bg_adj.a *= state.opacity;
    Color minor_adj     = minor;
    minor_adj.a *= state.opacity;
    major.a = std::min(1.0f, minor_adj.a * 4.0f);
    Color major_adj     = major;

    // ── Geometry constants ────────────────────────────────────────────────
    const f32 minor_step          = std::max(grid.spacing, 1.0f);
    const bool use_major          = grid.major_every > 1;
    const f32 major_step          = use_major ? minor_step * static_cast<f32>(grid.major_every) : minor_step;
    const f32 half_w              = static_cast<f32>(fb.width()) * 0.5f;
    const f32 half_h              = static_cast<f32>(fb.height()) * 0.5f;
    const f32 origin_x            = grid.centered ? half_w : 0.0f;
    const f32 origin_y            = grid.centered ? half_h : 0.0f;
    const f32 offset_x            = grid.offset.x;
    const f32 offset_y            = grid.offset.y;
    const f32 minor_thickness     = std::max(grid.minor_thickness, 0.0f);
    const f32 major_thickness     = std::max(grid.major_thickness, 0.0f);

    // ── Mask setup ────────────────────────────────────────────────────────
    const Color* mask_pixels = nullptr;
    i32 mask_w = 0;
    i32 mask_h = 0;
    if (state.mask && state.mask->enabled()) {
        ensure_mask_alpha_cache(state, fb.width(), fb.height());
        if (state.mask_alpha_cache) {
            mask_pixels = state.mask_alpha_cache->data();
            mask_w = state.mask_alpha_cache->width();
            mask_h = state.mask_alpha_cache->height();
        }
    }

    const i32 x0 = clip.x0;
    const i32 x1 = clip.x1;
    const i32 y0 = clip.y0;
    const i32 y1 = clip.y1;
    const i32 width = x1 - x0;

    // ── Precompute column weights (once per tile) ─────────────────────────
    //  We build the vector at full clip width so look-up is simply cols[x].
    std::vector<ColumnWeight> cols(static_cast<usize>(width));
    std::vector<i32> active_x;
    active_x.reserve(static_cast<usize>(width / 4));

    for (i32 x = x0; x < x1; ++x) {
        const f32 gx = static_cast<f32>(x) - origin_x - offset_x;

        const f32 minor_dx = cell_distance(gx, minor_step);
        const f32 major_dx = use_major ? cell_distance(gx, major_step) : 0.0f;

        ColumnWeight cw;
        cw.minor  = line_weight(minor_dx, minor_thickness);
        cw.major  = use_major ? line_weight(major_dx, major_thickness) : 0.0f;
        cw.active = (cw.minor > 0.0f || cw.major > 0.0f);

        cols[static_cast<usize>(x - x0)] = cw;
        if (cw.active) {
            active_x.push_back(x);
        }
    }

    // Early exit: no vertical grid lines anywhere in this clip region.
    if (active_x.empty() && !use_major) {
        // Even without verticals we still need to draw horizontal lines.
        // But if active_x is empty but rows might still be active, we handle it below.
    }

    // ── Parallel row fill ─────────────────────────────────────────────────
    fb.clear(bg_adj, clip);

    const bool has_active_cols = !active_x.empty();
    const i32 active_count = static_cast<i32>(active_x.size());

    tbb::parallel_for(tbb::blocked_range<i32>(y0, y1),
        [&](const tbb::blocked_range<i32>& range) {

        for (i32 y = range.begin(); y != range.end(); ++y) {
            Color* row = fb.pixels_row(y);

            const f32 gy          = static_cast<f32>(y) - origin_y - offset_y;
            const f32 row_minor_w = line_weight(cell_distance(gy, minor_step), minor_thickness);
            const f32 row_major_w = use_major
                ? line_weight(cell_distance(gy, major_step), major_thickness)
                : 0.0f;
            const bool row_active = (row_minor_w > 0.0f || row_major_w > 0.0f);

            if (row_active) {
                // ── Full horizontal scan: row has a grid line ──────────
                for (i32 x = x0; x < x1; ++x) {
                    if (mask_pixels) {
                        if (x < 0 || x >= mask_w || y < 0 || y >= mask_h) continue;
                        if (mask_pixels[static_cast<usize>(y) * mask_w + x].a <= 0.5f) continue;
                    }

                    const auto& cw = cols[static_cast<usize>(x - x0)];
                    const f32 minor_alpha = std::max(row_minor_w, cw.minor) * minor_adj.a;
                    const f32 major_alpha = std::max(row_major_w, cw.major) * major_adj.a;

                    if (major_alpha <= 0.0f && minor_alpha <= 0.0f) continue;

                    const f32 alpha = std::max(minor_alpha, major_alpha);
                    Color line = (major_alpha >= minor_alpha) ? major_adj : minor_adj;
                    line.a = alpha;
                    row[x] = compositor::blend_normal(line, row[x]);
                }
            } else if (has_active_cols) {
                // ── Sparse: only touch columns that have a vertical line ─
                for (i32 ci = 0; ci < active_count; ++ci) {
                    const i32 x = active_x[static_cast<usize>(ci)];
                    if (mask_pixels) {
                        if (x < 0 || x >= mask_w || y < 0 || y >= mask_h) continue;
                        if (mask_pixels[static_cast<usize>(y) * mask_w + x].a <= 0.5f) continue;
                    }

                    const auto& cw = cols[static_cast<usize>(x - x0)];
                    // row weights are zero since row_active == false
                    const f32 minor_alpha = cw.minor * minor_adj.a;
                    const f32 major_alpha = cw.major * major_adj.a;

                    if (major_alpha <= 0.0f && minor_alpha <= 0.0f) continue;

                    const f32 alpha = std::max(minor_alpha, major_alpha);
                    Color line = (major_alpha >= minor_alpha) ? major_adj : minor_adj;
                    line.a = alpha;
                    row[x] = compositor::blend_normal(line, row[x]);
                }
            }
            // else: no grid lines in this row or column → nothing to blend
        }
    });
}

} // namespace chronon3d::renderer
