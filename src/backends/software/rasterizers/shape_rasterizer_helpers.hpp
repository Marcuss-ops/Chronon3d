#pragma once

// ---------------------------------------------------------------------------
// shape_rasterizer_helpers.hpp
//
// Internal utility functions for shape rasterization:
// gradient resolution, shape size queries, stroke helpers, hit testing.
// Header-only (static inline) — shared across shape_rasterizer.cpp
// and potentially other rasterizer files.
// ---------------------------------------------------------------------------

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

// ── Gradient / fill helpers ────────────────────────────────────────

[[nodiscard]] static inline Color resolve_gradient_color(const Fill& fill, Vec2 lp, Vec2 sz, f32 opacity) {
    if (fill.type == FillType::Solid) {
        Color c = fill.solid.to_linear();
        c.a *= opacity;
        return c;
    }

    // Guard against zero-size sz which would produce NaN/Inf in norm
    // (e.g. rounded_rect with gradient fill and zero bbox).
    const f32 sx = std::max(sz.x, 1.0f);
    const f32 sy = std::max(sz.y, 1.0f);

    const Vec2 normalized{lp.x / sx, lp.y / sy};
    Color color = sample_gradient_at(fill.gradient, normalized);
    color.a *= opacity;
    return color;
}

// ── Shape property queries ─────────────────────────────────────────

[[nodiscard]] static inline Vec2 shape_size_for_fill(const Shape& shape) {
    switch (shape.type()) {
        case ShapeType::Rect:        return shape.rect().size;
        case ShapeType::RoundedRect: return shape.rounded_rect().size;
        case ShapeType::Circle:      return {shape.circle().radius * 2, shape.circle().radius * 2};
        case ShapeType::Image:       return shape.image().size;
        case ShapeType::TiledImage:  return shape.tiled_image().image.size;
        default:                     return {0, 0};
    }
}

/// Safe variant of shape_size_for_fill — guards against zero-size returns
/// (e.g. Text with box.enabled=false, or unknown ShapeType).  Falls back
/// to the shape's canonical size or {1,1} so gradient normalisation never
/// divides by zero.
[[nodiscard]] static inline Vec2 safe_shape_size_for_fill(const Shape& shape) {
    Vec2 sz = shape_size_for_fill(shape);
    if (sz.x <= 0.0f || sz.y <= 0.0f) {
        switch (shape.type()) {
            case ShapeType::Rect:        return shape.rect().size;
            case ShapeType::RoundedRect: return shape.rounded_rect().size;
            case ShapeType::Circle:      return {shape.circle().radius * 2, shape.circle().radius * 2};
            default:                     return {1.0f, 1.0f};
        }
    }
    return sz;
}

[[nodiscard]] static inline f32 stroke_width_for_shape(const Shape& shape) {
    switch (shape.type()) {
        case ShapeType::Rect:
            return shape.rect().stroke.enabled ? std::max(0.0f, shape.rect().stroke.width) : 0.0f;
        case ShapeType::RoundedRect:
            return shape.rounded_rect().stroke.enabled ? std::max(0.0f, shape.rounded_rect().stroke.width) : 0.0f;
        case ShapeType::Circle:
            return shape.circle().stroke.enabled ? std::max(0.0f, shape.circle().stroke.width) : 0.0f;
        default:
            return 0.0f;
    }
}

[[nodiscard]] static inline StrokeAlignment stroke_alignment_for_shape(const Shape& shape) {
    switch (shape.type()) {
        case ShapeType::Rect:
            return shape.rect().stroke.alignment;
        case ShapeType::RoundedRect:
            return shape.rounded_rect().stroke.alignment;
        case ShapeType::Circle:
            return shape.circle().stroke.alignment;
        default:
            return StrokeAlignment::Center;
    }
}

[[nodiscard]] static inline Color stroke_color_for_shape(const Shape& shape) {
    switch (shape.type()) {
        case ShapeType::Rect:
            return shape.rect().stroke.color.to_linear();
        case ShapeType::RoundedRect:
            return shape.rounded_rect().stroke.color.to_linear();
        case ShapeType::Circle:
            return shape.circle().stroke.color.to_linear();
        default:
            return Color{0.0f, 0.0f, 0.0f, 1.0f};
    }
}

/// Check whether a shape's stroke carries a gradient fill.
[[nodiscard]] static inline bool stroke_has_gradient(const Shape& shape) {
    switch (shape.type()) {
        case ShapeType::Rect:
            return shape.rect().stroke.gradient.has_value();
        case ShapeType::RoundedRect:
            return shape.rounded_rect().stroke.gradient.has_value();
        case ShapeType::Circle:
            return shape.circle().stroke.gradient.has_value();
        default:
            return false;
    }
}

/// Resolve the gradient colour at a local pixel position for a shape
/// stroke that carries a gradient.  Uses the same gradient sampling
/// logic as resolve_gradient_color but reads from the stroke gradient.
[[nodiscard]] static inline Color resolve_stroke_gradient_color(
    const Shape& shape, Vec2 lp, Vec2 sz)
{
    const std::optional<GradientFill>* g = nullptr;
    switch (shape.type()) {
        case ShapeType::Rect:
            g = &shape.rect().stroke.gradient;
            break;
        case ShapeType::RoundedRect:
            g = &shape.rounded_rect().stroke.gradient;
            break;
        case ShapeType::Circle:
            g = &shape.circle().stroke.gradient;
            break;
        default:
            return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (!g || !g->has_value()) {
        return Color{0.0f, 0.0f, 0.0f, 0.0f};
    }
    Fill fake_fill;
    fake_fill.type = fill_type_for_gradient(g->value().type);
    fake_fill.gradient = g->value();
    return resolve_gradient_color(fake_fill, lp, sz, 1.0f);
}

// ── Hit testing helpers ────────────────────────────────────────────

[[nodiscard]] static inline bool hit_test_rect_like(const Vec2& p, Vec2 size, f32 corner_radius, f32 spread) {
    if (corner_radius > 0.0f) {
        const f32 w = size.x;
        const f32 h = size.y;
        const f32 r = std::max(0.0f, std::min({corner_radius, w * 0.5f, h * 0.5f}));

        if (p.x < -spread || p.x > w + spread || p.y < -spread || p.y > h + spread) return false;

        const f32 r_spread = r + spread;
        if (p.x < r && p.y < r) {
            const f32 dx = p.x - r;
            const f32 dy = p.y - r;
            return (dx * dx + dy * dy) <= r_spread * r_spread;
        }
        if (p.x > w - r && p.y < r) {
            const f32 dx = p.x - (w - r);
            const f32 dy = p.y - r;
            return (dx * dx + dy * dy) <= r_spread * r_spread;
        }
        if (p.x < r && p.y > h - r) {
            const f32 dx = p.x - r;
            const f32 dy = p.y - (h - r);
            return (dx * dx + dy * dy) <= r_spread * r_spread;
        }
        if (p.x > w - r && p.y > h - r) {
            const f32 dx = p.x - (w - r);
            const f32 dy = p.y - (h - r);
            return (dx * dx + dy * dy) <= r_spread * r_spread;
        }
        return true;
    }

    return p.x >= -spread && p.x < size.x + spread &&
           p.y >= -spread && p.y < size.y + spread;
}

[[nodiscard]] static inline bool hit_test_per_corner_rect(
    const Vec2& p, Vec2 size, const std::array<f32, 4>& radii, f32 spread) {
    if (p.x < -spread || p.x > size.x + spread ||
        p.y < -spread || p.y > size.y + spread) return false;
    const std::size_t index = p.y < size.y * 0.5f
        ? (p.x < size.x * 0.5f ? 0u : 1u)
        : (p.x >= size.x * 0.5f ? 2u : 3u);
    const f32 radius = std::max(0.0f, std::min(radii[index], std::min(size.x, size.y) * 0.5f));
    if (radius <= 0.0f) return true;
    const bool in_corner = index == 0 ? p.x < radius && p.y < radius :
        index == 1 ? p.x > size.x - radius && p.y < radius :
        index == 2 ? p.x > size.x - radius && p.y > size.y - radius :
                     p.x < radius && p.y > size.y - radius;
    if (!in_corner) return true;
    const f32 cx = index == 0 || index == 3 ? radius : size.x - radius;
    const f32 cy = index < 2 ? radius : size.y - radius;
    const f32 expanded = radius + spread;
    const f32 dx = p.x - cx, dy = p.y - cy;
    return dx * dx + dy * dy <= expanded * expanded;
}

[[nodiscard]] static inline bool hit_test_shape_fill(const Shape& shape, Vec2 p, f32 spread, f32 corner_radius) {
    switch (shape.type()) {
        case ShapeType::Rect:
            return hit_test_rect_like(p, shape.rect().size, corner_radius, spread);
        case ShapeType::RoundedRect:
            if (!shape.rounded_rect().corner_radius)
                return hit_test_rect_like(p, shape.rounded_rect().size, shape.rounded_rect().radius, spread);
            return hit_test_per_corner_rect(p, shape.rounded_rect().size,
                                            *shape.rounded_rect().corner_radius, spread);
        case ShapeType::Circle: {
            const f32 r = shape.circle().radius + spread;
            const f32 dx = p.x - shape.circle().radius;
            const f32 dy = p.y - shape.circle().radius;
            return (dx * dx + dy * dy) <= r * r;
        }
        default:
            return false;
    }
}

[[nodiscard]] static inline bool hit_test_shape_stroke(const Shape& shape, Vec2 p, f32 spread, f32 corner_radius) {
    const f32 stroke = stroke_width_for_shape(shape);
    if (stroke <= 0.0f) {
        return false;
    }

    const auto alignment = stroke_alignment_for_shape(shape);
    const f32 half = stroke * 0.5f;
    const f32 outer_spread = alignment == StrokeAlignment::Inside ? spread : spread + half;
    const f32 inner_spread = alignment == StrokeAlignment::Outside ? spread : std::max(0.0f, spread - half);

    // ── Origin offsets for inner/outer rects ────────────────────────────
    //
    // hit_test_rect_like assumes the rect starts at (0, 0).  For strokes
    // that extend symmetrically past the rect edge (Center, Outside) we
    // must shift p so the virtual rect covers the correct region.
    //
    //   Inside   — outer = rect itself (no shift); inner = rect inset by
    //              `stroke` from each edge → shift inner origin by (stroke, stroke)
    //   Center   — stroke is centred on the rect edge: outer extends half
    //              outward (shift origin by -half), inner is inset by half
    //              (shift origin by +half)
    //   Outside  — stroke extends outward: outer shifted by -half, inner
    //              = rect itself (no shift)

    Vec2 outer_origin{0, 0}, inner_origin{0, 0};
    if (alignment == StrokeAlignment::Center) {
        outer_origin = {-half, -half};
        inner_origin = { half,  half};
    } else if (alignment == StrokeAlignment::Outside) {
        outer_origin = {-half, -half};
    } else { // Inside
        inner_origin = {stroke, stroke};
    }

    switch (shape.type()) {
        case ShapeType::Rect: {
            const Vec2 outer_size = alignment == StrokeAlignment::Inside ? shape.rect().size : shape.rect().size + Vec2{stroke, stroke};
            const Vec2 inner_size = alignment == StrokeAlignment::Outside ? shape.rect().size : Vec2{std::max(0.0f, shape.rect().size.x - stroke), std::max(0.0f, shape.rect().size.y - stroke)};
            const f32 outer_radius = alignment == StrokeAlignment::Inside ? corner_radius : std::max(0.0f, corner_radius + half);
            const f32 inner_radius = alignment == StrokeAlignment::Outside ? corner_radius : std::max(0.0f, corner_radius - half);
            return hit_test_rect_like(p - outer_origin, outer_size, outer_radius, outer_spread) &&
                   !hit_test_rect_like(p - inner_origin, inner_size, inner_radius, inner_spread);
        }
        case ShapeType::RoundedRect: {
            const Vec2 outer_size = alignment == StrokeAlignment::Inside ? shape.rounded_rect().size : shape.rounded_rect().size + Vec2{stroke, stroke};
            const Vec2 inner_size = alignment == StrokeAlignment::Outside ? shape.rounded_rect().size : Vec2{std::max(0.0f, shape.rounded_rect().size.x - stroke), std::max(0.0f, shape.rounded_rect().size.y - stroke)};
            if (shape.rounded_rect().corner_radius) {
                auto outer_radii = *shape.rounded_rect().corner_radius;
                auto inner_radii = *shape.rounded_rect().corner_radius;
                for (std::size_t i = 0; i < outer_radii.size(); ++i) {
                    if (alignment != StrokeAlignment::Inside) outer_radii[i] += half;
                    if (alignment != StrokeAlignment::Outside)
                        inner_radii[i] = std::max(0.0f, inner_radii[i] - half);
                }
                return hit_test_per_corner_rect(p - outer_origin, outer_size, outer_radii, outer_spread) &&
                       !hit_test_per_corner_rect(p - inner_origin, inner_size, inner_radii, inner_spread);
            }
            const f32 outer_radius = alignment == StrokeAlignment::Inside ? shape.rounded_rect().radius : shape.rounded_rect().radius + half;
            const f32 inner_radius = alignment == StrokeAlignment::Outside ? shape.rounded_rect().radius : std::max(0.0f, shape.rounded_rect().radius - half);
            return hit_test_rect_like(p - outer_origin, outer_size, outer_radius, outer_spread) &&
                   !hit_test_rect_like(p - inner_origin, inner_size, inner_radius, inner_spread);
        }
        case ShapeType::Circle: {
            const f32 outer_radius = alignment == StrokeAlignment::Inside ? shape.circle().radius : shape.circle().radius + half;
            const f32 inner_radius = alignment == StrokeAlignment::Outside ? shape.circle().radius : std::max(0.0f, shape.circle().radius - half);
            const f32 outer_r = outer_radius + outer_spread;
            const f32 inner_r = inner_radius + inner_spread;
            const f32 dx = p.x - shape.circle().radius;
            const f32 dy = p.y - shape.circle().radius;
            const f32 dist2 = dx * dx + dy * dy;
            return dist2 <= outer_r * outer_r && dist2 >= inner_r * inner_r;
        }
        default:
            return false;
    }
}

} // namespace chronon3d::renderer
