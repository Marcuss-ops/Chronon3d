#include "shape_rasterizer.hpp"
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <chronon3d/scene/fill.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/simd/kernels.hpp>
#include "path_rasterizer.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {
namespace renderer {

namespace {

Color resolve_gradient_color(const Fill& fill, Vec2 lp, Vec2 sz, f32 opacity) {
    if (fill.type == FillType::Solid) {
        Color c = fill.solid.to_linear();
        c.a *= opacity;
        return c;
    }

    f32 t = 0.0f;
    if (fill.type == FillType::LinearGradient) {
        const Vec2 norm = { (lp.x / sz.x), (lp.y / sz.y) };
        const Vec2 dir = fill.gradient.to - fill.gradient.from;
        const f32 len_sq = dir.x * dir.x + dir.y * dir.y;
        if (len_sq > 1e-6f) {
            const Vec2 rel = norm - fill.gradient.from;
            t = (rel.x * dir.x + rel.y * dir.y) / len_sq;
        }
    } else if (fill.type == FillType::RadialGradient) {
        const Vec2 norm = { (lp.x / sz.x), (lp.y / sz.y) };
        const Vec2 d = norm - fill.gradient.from;
        const Vec2 rv = fill.gradient.to - fill.gradient.from;
        const f32 r = glm::length(rv);
        t = (r > 1e-6f) ? glm::length(d) / r : 0.0f;
    } else if (fill.type == FillType::ConicGradient) {
        const Vec2 norm = { (lp.x / sz.x), (lp.y / sz.y) };
        const Vec2 d = norm - fill.gradient.from;
        float angle = std::atan2(d.y, d.x);
        if (angle < 0.0f) {
            angle += 2.0f * 3.14159265f;
        }
        const Vec2 dir = fill.gradient.to - fill.gradient.from;
        float start_angle = std::atan2(dir.y, dir.x);
        if (start_angle < 0.0f) {
            start_angle += 2.0f * 3.14159265f;
        }
        float relative_angle = angle - start_angle;
        if (relative_angle < 0.0f) {
            relative_angle += 2.0f * 3.14159265f;
        }
        t = relative_angle / (2.0f * 3.14159265f);
    } else {
        Color c = fill.solid.to_linear();
        c.a *= opacity;
        return c;
    }

    t = std::clamp(t, 0.0f, 1.0f);

    if (fill.gradient.stops.empty()) {
        Color c = fill.solid.to_linear();
        c.a *= opacity;
        return c;
    }

    Color c;
    if (t <= fill.gradient.stops.front().offset) {
        c = fill.gradient.stops.front().color.to_linear();
    } else if (t >= fill.gradient.stops.back().offset) {
        c = fill.gradient.stops.back().color.to_linear();
    } else {
        for (size_t i = 0; i < fill.gradient.stops.size() - 1; ++i) {
            const auto& a = fill.gradient.stops[i];
            const auto& b = fill.gradient.stops[i+1];
            if (t >= a.offset && t <= b.offset) {
                const f32 range = b.offset - a.offset;
                const f32 local_t = (range < 1e-6f) ? 0.0f : (t - a.offset) / range;
                Color c_a = a.color.to_linear();
                Color c_b = b.color.to_linear();
                c = c_a + (c_b - c_a) * local_t;
                break;
            }
        }
    }

    c.a *= opacity;
    return c;
}

Vec2 shape_size_for_fill(const Shape& shape) {
    switch (shape.type) {
        case ShapeType::Rect:        return shape.rect.size;
        case ShapeType::RoundedRect: return shape.rounded_rect.size;
        case ShapeType::Circle:      return {shape.circle.radius * 2, shape.circle.radius * 2};
        case ShapeType::Image:       return shape.image.size;
        case ShapeType::Text:        return shape.text.box.enabled ? shape.text.box.size : Vec2{0.0f, 0.0f};
        default:                     return {0, 0};
    }
}

} // namespace

bool pixel_passes_mask(const RenderState& state, i32 x, i32 y) {
    if (!state.mask || !state.mask->enabled() || !state.mask_alpha_cache) return true;
    const Color* mask_pixels = state.mask_alpha_cache->data();
    const i32 mw = state.mask_alpha_cache->width();
    const i32 mh = state.mask_alpha_cache->height();
    if (x < 0 || x >= mw || y < 0 || y >= mh) return false;
    return mask_pixels[static_cast<size_t>(y) * mw + x].a > 0.5f;
}

raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) {
    if (shape.type == ShapeType::Line) {
        Vec4 p0 = model * Vec4(0, 0, 0, 1);
        Vec4 p1 = model * Vec4(shape.line.to, 1);
        const f32 pad = spread + std::max(kBBoxSafetyPadding, shape.line.thickness * 0.5f);
        return {
            static_cast<i32>(std::floor(std::min(p0.x, p1.x) - pad)),
            static_cast<i32>(std::floor(std::min(p0.y, p1.y) - pad)),
            static_cast<i32>(std::ceil(std::max(p0.x, p1.x) + pad)),
            static_cast<i32>(std::ceil(std::max(p0.y, p1.y) + pad))
        };
    }

    Vec2 size{0, 0};
    switch (shape.type) {
        case ShapeType::Rect:        size = shape.rect.size; break;
        case ShapeType::RoundedRect: size = shape.rounded_rect.size; break;
        case ShapeType::Circle:      size = {shape.circle.radius * 2, shape.circle.radius * 2}; break;
        case ShapeType::Image:       size = shape.image.size; break;
        case ShapeType::GridBackground: size = shape.grid_background.size; break;
        case ShapeType::Text:        size = shape.text.box.enabled ? shape.text.box.size : Vec2{1000, 1000}; break; // Heuristic for text
        case ShapeType::FakeBox3D:   size = shape.fake_box3d.size; break;
        default: break;
    }

    const f32 pad = spread + kBBoxSafetyPadding;

    const Mat4 effective_model = (shape.type == ShapeType::GridBackground) ? Mat4(1.0f) : model;

    const Vec4 corners[4] = {
        effective_model * Vec4{-pad, -pad, 0, 1},
        effective_model * Vec4{size.x + pad, -pad, 0, 1},
        effective_model * Vec4{size.x + pad, size.y + pad, 0, 1},
        effective_model * Vec4{-pad, size.y + pad, 0, 1}
    };

    f32 min_x = 1e10f, min_y = 1e10f, max_x = -1e10f, max_y = -1e10f;
    for (const auto& c : corners) {
        if (std::abs(c.w) < 1e-7f) continue;
        const f32 x = c.x / c.w;
        const f32 y = c.y / c.w;
        min_x = std::min(min_x, x); min_y = std::min(min_y, y);
        max_x = std::max(max_x, x); max_y = std::max(max_y, y);
    }

    return {
        static_cast<i32>(std::floor(min_x)),
        static_cast<i32>(std::floor(min_y)),
        static_cast<i32>(std::ceil(max_x)),
        static_cast<i32>(std::ceil(max_y))
    };
}

bool hit_test(const Shape& s, Vec2 p, f32 spread, f32 corner_radius) {
    switch (s.type) {
        case ShapeType::Rect: {
            if (corner_radius > 0.0f) {
                const f32 w = s.rect.size.x;
                const f32 h = s.rect.size.y;
                const f32 r = std::max(0.0f, std::min({corner_radius, w * 0.5f, h * 0.5f}));
                
                if (p.x < -spread || p.x > w + spread || p.y < -spread || p.y > h + spread) return false;

                const f32 r_spread = r + spread;
                if (p.x < r && p.y < r) { // Top-left
                    const f32 dx = p.x - r;
                    const f32 dy = p.y - r;
                    return (dx * dx + dy * dy) <= r_spread * r_spread;
                }
                if (p.x > w - r && p.y < r) { // Top-right
                    const f32 dx = p.x - (w - r);
                    const f32 dy = p.y - r;
                    return (dx * dx + dy * dy) <= r_spread * r_spread;
                }
                if (p.x < r && p.y > h - r) { // Bottom-left
                    const f32 dx = p.x - r;
                    const f32 dy = p.y - (h - r);
                    return (dx * dx + dy * dy) <= r_spread * r_spread;
                }
                if (p.x > w - r && p.y > h - r) { // Bottom-right
                    const f32 dx = p.x - (w - r);
                    const f32 dy = p.y - (h - r);
                    return (dx * dx + dy * dy) <= r_spread * r_spread;
                }
                return true;
            }
            return p.x >= -spread && p.x < s.rect.size.x + spread &&
                   p.y >= -spread && p.y < s.rect.size.y + spread;
        }

        case ShapeType::RoundedRect: {
            const f32 w = s.rounded_rect.size.x;
            const f32 h = s.rounded_rect.size.y;
            const f32 r = std::max(0.0f, std::min({s.rounded_rect.radius, w * 0.5f, h * 0.5f}));
            
            if (p.x < -spread || p.x > w + spread || p.y < -spread || p.y > h + spread) return false;

            const f32 r_spread = r + spread;
            if (p.x < r && p.y < r) { // Top-left
                const f32 dx = p.x - r;
                const f32 dy = p.y - r;
                return (dx * dx + dy * dy) <= r_spread * r_spread;
            }
            if (p.x > w - r && p.y < r) { // Top-right
                const f32 dx = p.x - (w - r);
                const f32 dy = p.y - r;
                return (dx * dx + dy * dy) <= r_spread * r_spread;
            }
            if (p.x < r && p.y > h - r) { // Bottom-left
                const f32 dx = p.x - r;
                const f32 dy = p.y - (h - r);
                return (dx * dx + dy * dy) <= r_spread * r_spread;
            }
            if (p.x > w - r && p.y > h - r) { // Bottom-right
                const f32 dx = p.x - (w - r);
                const f32 dy = p.y - (h - r);
                return (dx * dx + dy * dy) <= r_spread * r_spread;
            }
            return true;
        }

        case ShapeType::Circle: {
            const f32 r = s.circle.radius + spread;
            const f32 dx = p.x - s.circle.radius;
            const f32 dy = p.y - s.circle.radius;
            return (dx * dx + dy * dy) <= r * r;
        }

        default:
            return true;
    }
}

void draw_transformed_shape(Framebuffer& fb, const Shape& shape, const Mat4& model, const Color& color,
                             f32 spread, const RenderState* state, const Fill* fill, f32 corner_radius) {
    if (color.a <= 0.0f) return;

    if (shape.type == ShapeType::Path) {
        (void)fill;
        draw_path(fb, shape.path, model, color, state);
        return;
    }

    raster::BBox bbox = compute_world_bbox(shape, model, spread);
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

#ifdef CHRONON_DEBUG_VERBOSE
    if (shape.type == ShapeType::Rect) {
        spdlog::info("DEBUG DRAW SHAPE: bbox=[{}, {}, {}, {}] model_trans=[{}, {}] size=[{}, {}] fb=[{}, {}]",
                     bbox.x0, bbox.y0, bbox.x1, bbox.y1, model[3][0], model[3][1],
                     shape.rect.size.x, shape.rect.size.y, fb.width(), fb.height());
    }
#endif

    glm::mat3 H;
    H[0][0] = model[0][0]; H[0][1] = model[0][1]; H[0][2] = model[0][3];
    H[1][0] = model[1][0]; H[1][1] = model[1][1]; H[1][2] = model[1][3];
    H[2][0] = model[3][0]; H[2][1] = model[3][1]; H[2][2] = model[3][3];

    glm::mat3 invH = glm::inverse(H);

    const bool use_gradient = fill && fill->type != FillType::Solid;
    const Vec2 sz = use_gradient ? shape_size_for_fill(shape) : Vec2{0, 0};

    const Vec3 col0 = invH[0];
    const Vec3 col1 = invH[1];
    const Vec3 col2 = invH[2];

    const bool can_use_simd = !use_gradient && (corner_radius <= 0.0f) && (!state || !state->mask || !state->mask->enabled());

    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        Color* row = fb.pixels_row(y);
        
        Vec3 lp_h = col0 * (static_cast<f32>(bbox.x0) + 0.5f) +
                    col1 * (static_cast<f32>(y) + 0.5f) +
                    col2;

        i32 x = bbox.x0;
        
        if (can_use_simd && shape.type == ShapeType::Rect) {
            const i32 count = bbox.x1 - x;
            const i32 simd_count = count & ~3;
            if (simd_count > 0) {
                simd::rasterize_rect_simd(row + x, &lp_h.x, &col0.x, simd_count, 
                                          shape.rect.size.x, shape.rect.size.y, spread, color);
                x += simd_count;
                lp_h += col0 * static_cast<f32>(simd_count);
            }
        }

        for (; x < bbox.x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) {
                lp_h += col0;
                continue;
            }

            if (std::abs(lp_h.z) >= 1e-7f) {
                const f32 inv_z = 1.0f / lp_h.z;
                Vec2 lp(lp_h.x * inv_z, lp_h.y * inv_z);

                if (hit_test(shape, lp, spread, corner_radius)) {
                    const Color pixel_color = use_gradient
                        ? resolve_gradient_color(*fill, lp, sz, color.a)
                        : color;

                    // Guard: skip NaN/Inf pixels to prevent framebuffer contamination.
                    if (!std::isnan(pixel_color.r) && !std::isnan(pixel_color.g) &&
                        !std::isnan(pixel_color.b) && !std::isnan(pixel_color.a) &&
                        !std::isinf(pixel_color.r) && !std::isinf(pixel_color.g) &&
                        !std::isinf(pixel_color.b) && !std::isinf(pixel_color.a)) {
                        row[x] = compositor::blend(pixel_color, row[x], BlendMode::Normal);
                    }
                }
            }
            
            lp_h += col0;
        }
    }
}

} // namespace renderer
} // namespace chronon3d
