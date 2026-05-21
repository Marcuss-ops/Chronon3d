#include "glass_panel_renderer.hpp"
#include "../rasterizers/shape_rasterizer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d {
namespace renderer {

void draw_glass_panel(Framebuffer& fb, const Framebuffer& src, const Shape& shape, const Mat4& model, f32 opacity, const RenderState* state) {
    raster::BBox bbox = compute_world_bbox(shape, model, 0.0f);
    if (state && state->clip_rect) {
        bbox = raster::BBox{
            .x0 = std::max(bbox.x0, state->clip_rect->x0),
            .y0 = std::max(bbox.y0, state->clip_rect->y0),
            .x1 = std::min(bbox.x1, state->clip_rect->x1),
            .y1 = std::min(bbox.y1, state->clip_rect->y1)
        };
    }
    bbox.clip_to(fb.width(), fb.height());
    if (bbox.is_empty()) return;
    if (state && state->mask && state->mask->enabled()) {
        ensure_mask_alpha_cache(*state, fb.width(), fb.height());
    }
    Mat4 inv_model = glm::inverse(model);
    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        Color* dst_row = fb.pixels_row(y);
        const Color* src_row = src.pixels_row(y);
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) continue;
            Vec4 lp = inv_model * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
            if (hit_test(shape, Vec2(lp.x, lp.y), 0.0f)) {
                Color c = src_row[x];
                // Subtle frost effect
                c.r = c.r * 0.95f + 0.05f;
                c.g = c.g * 0.95f + 0.05f;
                c.b = c.b * 0.95f + 0.05f;
                c.a *= opacity;
                dst_row[x] = compositor::blend(c, dst_row[x], BlendMode::Normal);
            }
        }
    }
}

} // namespace renderer
} // namespace chronon3d
