#include "shape_rasterizer.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <chronon3d/scene/fill.hpp>
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

    f32 min_x = 1e10f, max_x = -1e10f;
    f32 min_y = 1e10f, max_y = -1e10f;

    for (int i = 0; i < 4; ++i) {
        const f32 w = corners[i].w;
        if (std::abs(w) < 1e-7f) continue;
        const f32 px = corners[i].x / w;
        const f32 py = corners[i].y / w;
        min_x = std::min(min_x, px);
        max_x = std::max(max_x, px);
        min_y = std::min(min_y, py);
        max_y = std::max(max_y, py);
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

// Sample gradient color for a local shape point, in linear space.
// base_alpha: caller-supplied alpha with opacity already applied.
static Color resolve_gradient_color(const Fill& fill, Vec2 local_pt, Vec2 shape_size, f32 base_alpha) {
    // Normalise local_pt to [0..1] over shape_size.
    const f32 nx = (shape_size.x > 0.0f) ? (local_pt.x / shape_size.x) : 0.0f;
    const f32 ny = (shape_size.y > 0.0f) ? (local_pt.y / shape_size.y) : 0.0f;

    f32 t = 0.0f;
    if (fill.type == FillType::LinearGradient) {
        const Vec2 dir = fill.gradient.to - fill.gradient.from;
        const f32 len2 = dir.x * dir.x + dir.y * dir.y;
        if (len2 > 1e-10f) {
            const Vec2 d{nx - fill.gradient.from.x, ny - fill.gradient.from.y};
            t = (d.x * dir.x + d.y * dir.y) / len2;
        }
    } else { // RadialGradient
        const Vec2 d{nx - fill.gradient.from.x, ny - fill.gradient.from.y};
        const Vec2 rv{fill.gradient.to - fill.gradient.from};
        const f32 r = std::sqrt(rv.x * rv.x + rv.y * rv.y);
        t = (r > 1e-6f) ? (std::sqrt(d.x * d.x + d.y * d.y) / r) : 0.0f;
    }

    t = std::clamp(t, 0.0f, 1.0f);
    Color c = sample_gradient(fill.gradient, t).to_linear();
    c.a *= base_alpha;
    return c;
}

static Vec2 shape_size_for_fill(const Shape& shape) {
    switch (shape.type) {
        case ShapeType::Rect:        return shape.rect.size;
        case ShapeType::RoundedRect: return shape.rounded_rect.size;
        case ShapeType::Circle:      return {shape.circle.radius * 2.0f, shape.circle.radius * 2.0f};
        case ShapeType::Image:       return shape.image.size;
        default:                     return {0, 0};
    }
}

void draw_transformed_shape(Framebuffer& fb, const Shape& shape, const Mat4& model, const Color& color,
                             f32 spread, const RenderState* state, const Fill* fill) {
    if (color.a <= 0.0f) return;

    raster::BBox bbox = compute_world_bbox(shape, model, spread);
    bbox.clip_to(fb.width(), fb.height());

    if (bbox.is_empty()) return;

    // Extract 3x3 homography for local_z = 0 plane
    glm::mat3 H;
    H[0][0] = model[0][0]; H[0][1] = model[0][1]; H[0][2] = model[0][3];
    H[1][0] = model[1][0]; H[1][1] = model[1][1]; H[1][2] = model[1][3];
    H[2][0] = model[3][0]; H[2][1] = model[3][1]; H[2][2] = model[3][3];

    glm::mat3 invH = glm::inverse(H);

    const bool use_gradient = fill && fill->type != FillType::Solid;
    const Vec2 sz = use_gradient ? shape_size_for_fill(shape) : Vec2{0, 0};

    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) continue;

            Vec3 lp_h = invH * Vec3(static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, 1.0f);
            if (std::abs(lp_h.z) < 1e-7f) continue;
            Vec2 lp(lp_h.x / lp_h.z, lp_h.y / lp_h.z);

            if (!hit_test(shape, lp, spread)) continue;

            // color is already in linear space with opacity applied by the caller.
            const Color pixel_color = use_gradient
                ? resolve_gradient_color(*fill, lp, sz, color.a)
                : color;

            fb.set_pixel(x, y, compositor::blend(pixel_color, fb.get_pixel(x, y), BlendMode::Normal));
        }
    }
}

} // namespace renderer
} // namespace chronon3d
