#include "shape_rasterizer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) {
    Vec2 size{0, 0};
    switch (shape.type) {
        case ShapeType::Rect: size = shape.rect.size; break;
        case ShapeType::RoundedRect: size = shape.rounded_rect.size; break;
        case ShapeType::Circle: size = Vec2{shape.circle.radius * 2, shape.circle.radius * 2}; break;
        case ShapeType::Image: size = shape.image.size; break;
        case ShapeType::FakeBox3D: size = shape.fake_box3d.size; break;
        case ShapeType::GridPlane: size = {shape.grid_plane.extent * 2, shape.grid_plane.extent * 2}; break;
        case ShapeType::FakeExtrudedText: size = {shape.fake_extruded_text.font_size * 20, shape.fake_extruded_text.font_size * 4}; break;
        default: break;
    }

    if (size.x == 0 && size.y == 0) return {0, 0, 0, 0};

    Vec2 min_local{-spread, -spread};
    Vec2 max_local{size.x + spread, size.y + spread};

    Vec4 corners[4] = {
        model * Vec4(min_local.x, min_local.y, 0, 1),
        model * Vec4(max_local.x, min_local.y, 0, 1),
        model * Vec4(max_local.x, max_local.y, 0, 1),
        model * Vec4(min_local.x, max_local.y, 0, 1)
    };

    f32 min_x = corners[0].x, max_x = corners[0].x;
    f32 min_y = corners[0].y, max_y = corners[0].y;

    for (int i = 1; i < 4; ++i) {
        min_x = std::min(min_x, corners[i].x);
        max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y);
        max_y = std::max(max_y, corners[i].y);
    }

    return {
        static_cast<i32>(std::floor(min_x)),
        static_cast<i32>(std::floor(min_y)),
        static_cast<i32>(std::ceil(max_x)) + 1,
        static_cast<i32>(std::ceil(max_y)) + 1
    };
}

bool hit_test(const Shape& s, Vec2 p, f32 spread) {
    switch (s.type) {
        case ShapeType::Rect:
            return p.x >= -spread && p.x < s.rect.size.x + spread &&
                   p.y >= -spread && p.y < s.rect.size.y + spread;
        case ShapeType::RoundedRect: {
            f32 w = s.rounded_rect.size.x;
            f32 h = s.rounded_rect.size.y;
            f32 r = std::min(s.rounded_rect.radius + spread, std::min(w, h) * 0.5f);
            if (p.x < -spread || p.x >= w + spread || p.y < -spread || p.y >= h + spread) return false;
            
            if (p.x < r && p.y < r) return glm::length(Vec2(p.x - r, p.y - r)) < r;
            if (p.x > w - r && p.y < r) return glm::length(Vec2(p.x - (w - r), p.y - r)) < r;
            if (p.x > w - r && p.y > h - r) return glm::length(Vec2(p.x - (w - r), p.y - (h - r))) < r;
            if (p.x < r && p.y > h - r) return glm::length(Vec2(p.x - r, p.y - (h - r))) < r;
            return true;
        }
        case ShapeType::Circle: {
            f32 dx = p.x - s.circle.radius;
            f32 dy = p.y - s.circle.radius;
            return (dx * dx + dy * dy) <= (s.circle.radius + spread) * (s.circle.radius + spread);
        }
        case ShapeType::Image:
            return p.x >= -spread && p.x < s.image.size.x + spread &&
                   p.y >= -spread && p.y < s.image.size.y + spread;
        default: return false;
    }
}

bool pixel_passes_mask(const RenderState& state, i32 x, i32 y) {
    if (!state.mask || !state.mask->enabled()) return true;
    Vec4 local = state.layer_inv_matrix * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
    return mask_contains_local_point(*state.mask, Vec2{local.x, local.y});
}

void draw_transformed_shape(Framebuffer& fb, const Shape& shape, const Mat4& model, const Color& color,
                             f32 spread, const RenderState* state) {
    if (color.a <= 0.0f) return;

    raster::BBox bbox = compute_world_bbox(shape, model, spread);
    bbox.clip_to(fb.width(), fb.height());

    if (bbox.is_empty()) return;

    Mat4 inv_model = glm::inverse(model);

    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) continue;
            Vec4 lp_h = inv_model * Vec4(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 0.0f, 1.0f);
            Vec2 lp(lp_h.x / lp_h.w, lp_h.y / lp_h.w);
            
            if (hit_test(shape, lp, spread)) {
                fb.set_pixel(x, y, compositor::blend(color, fb.get_pixel(x, y), BlendMode::Normal));
            }
        }
    }
}

} // namespace renderer
} // namespace chronon3d
