#include "glass_panel_renderer.hpp"
#include "../rasterizers/shape_rasterizer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d {
namespace renderer {

void draw_glass_panel(Framebuffer& fb, const Framebuffer& src, const Shape& shape, const Mat4& model, f32 opacity, const RenderState* state) {
    raster::BBox bbox = compute_world_bbox(shape, model, 0.0f);
    bbox.clip_to(fb.width(), fb.height());
    if (bbox.is_empty()) return;
    Mat4 inv_model = glm::inverse(model);
    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) continue;
            Vec4 lp = inv_model * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
            if (hit_test(shape, Vec2(lp.x, lp.y), 0.0f)) {
                Color c = src.get_pixel(x, y);
                // Subtle frost effect
                c.r = c.r * 0.95f + 0.05f;
                c.g = c.g * 0.95f + 0.05f;
                c.b = c.b * 0.95f + 0.05f;
                c.a *= opacity;
                fb.set_pixel(x, y, compositor::blend(c, fb.get_pixel(x, y), BlendMode::Normal));
            }
        }
    }
}

} // namespace renderer
} // namespace chronon3d
