#include <chronon3d/backends/software/rasterizers/path_rasterizer.hpp>
#include "path_rasterizer.hpp"

// Sub-module headers
#include "path_cache.hpp"
#include "path_stroke.hpp"
#include "path_utils.hpp"
#include "pip.hpp"

#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

#include "shape_rasterizer.hpp"

namespace chronon3d::renderer {

raster::BBox compute_path_bbox(const PathShape& path, const Mat4& model, f32 spread) {
    const auto contours = flatten_path_cached(path);
    if (contours.empty()) return {0, 0, 0, 0};

    f32 min_x = 1e10f;
    f32 min_y = 1e10f;
    f32 max_x = -1e10f;
    f32 max_y = -1e10f;
    const f32 radius = std::max(0.0f, path.stroke.width * 0.5f) + spread + 1.0f;

    for (const auto& contour : contours) {
        for (const auto& p : contour.points) {
            const Vec2 s = transform_point(model, p);
            min_x = std::min(min_x, s.x - radius);
            min_y = std::min(min_y, s.y - radius);
            max_x = std::max(max_x, s.x + radius);
            max_y = std::max(max_y, s.y + radius);
        }
    }

    if (min_x > max_x || min_y > max_y) return {0, 0, 0, 0};

    return {
        static_cast<i32>(std::floor(min_x)),
        static_cast<i32>(std::floor(min_y)),
        static_cast<i32>(std::ceil(max_x)) + 1,
        static_cast<i32>(std::ceil(max_y)) + 1,
    };
}

void draw_path(Framebuffer& fb, const PathShape& path, const Mat4& model, const Color& stroke_color,
               const RenderState* state) {
    const auto contours = flatten_path_cached(path);
    if (contours.empty()) return;

    std::vector<PathContour> screen_contours;
    screen_contours.reserve(contours.size());
    for (const auto& contour : contours) {
        PathContour projected;
        projected.closed = contour.closed;
        projected.points.reserve(contour.points.size());
        for (const auto& p : contour.points) {
            projected.points.push_back(transform_point(model, p));
        }
        screen_contours.push_back(std::move(projected));
    }

    raster::BBox bbox = compute_path_bbox(path, model, 0.0f);
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

    const f32 radius = std::max(0.0f, path.stroke.width * 0.5f);
    const f32 opacity = stroke_color.a;
    const bool fill_enabled = path.fill.enabled;

    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) continue;

            const Vec2 p{static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f};

            if (fill_enabled) {
                bool inside = false;
                const PipMode mode = get_pip_mode();
                const bool prefetch = is_prefetch_enabled();
                for (size_t c_idx = 0; c_idx < screen_contours.size(); ++c_idx) {
                    if (prefetch && c_idx + 1 < screen_contours.size()) {
                        chrono_prefetch(&screen_contours[c_idx + 1]);
                        if (!screen_contours[c_idx + 1].points.empty()) {
                            chrono_prefetch(screen_contours[c_idx + 1].points.data());
                        }
                    }
                    const auto& contour = screen_contours[c_idx];
                    if (contour.closed) {
                        bool c_inside = false;
                        if (mode == PipMode::Simd) {
                            c_inside = point_in_polygon_avx2(p, contour.points);
                        } else {
                            c_inside = point_in_polygon_even_odd(p, contour.points);
                        }
                        if (c_inside) {
                            inside = !inside;
                        }
                    }
                }
                if (inside) {
                    const Color pixel_color = resolve_fill_color(path.fill, p, bbox, opacity);
                    row[x] = compositor::blend(pixel_color, row[x], BlendMode::Normal);
                    continue;
                }
            }

            if (path.stroke.enabled) {
                bool hit = false;
                Color pixel_color{
                    stroke_color.r * path.stroke.color.r,
                    stroke_color.g * path.stroke.color.g,
                    stroke_color.b * path.stroke.color.b,
                    stroke_color.a * path.stroke.color.a
                };
                for (const auto& contour : screen_contours) {
                    if (contour.points.size() < 2) continue;

                    const auto trimmed = trim_polyline_points(
                        contour.points,
                        contour.closed,
                        path.stroke.trim_start,
                        path.stroke.trim_end);
                    const auto& pts = trimmed.empty() ? contour.points : trimmed;
                    if (pts.size() < 2) continue;

                    std::vector<std::vector<Vec2>> sub_polylines;
                    if (path.stroke.dash_array.empty()) {
                        sub_polylines.push_back(pts);
                    } else {
                        sub_polylines = dash_polyline_points(pts, false, path.stroke.dash_array, path.stroke.dash_offset);
                    }

                    for (const auto& pts_sub : sub_polylines) {
                        if (pts_sub.size() < 2) continue;

                        const bool closed = path.stroke.dash_array.empty() ? contour.closed : false;
                        const bool open = !closed;
                        const bool repeated_start = closed && pts_sub.size() > 2 &&
                            glm::length(pts_sub.front() - pts_sub.back()) < 1e-4f;
                        const usize unique_count = repeated_start ? pts_sub.size() - 1 : pts_sub.size();

                        for (usize i = 0; i + 1 < pts_sub.size(); ++i) {
                            Vec2 a = pts_sub[i];
                            Vec2 b = pts_sub[i + 1];
                            if (path.stroke.cap == LineCap::Square && open && i == 0) {
                                const Vec2 dir = glm::normalize(b - a);
                                a -= dir * radius;
                            }
                            if (path.stroke.cap == LineCap::Square && open && i + 2 == pts_sub.size()) {
                                const Vec2 dir = glm::normalize(b - a);
                                b += dir * radius;
                            }

                            const f32 cov = segment_coverage(p, a, b, radius);
                            if (cov > 0.0f) {
                                hit = true;
                                pixel_color.a *= cov * opacity;
                                break;
                            }
                        }

                        if (!hit && path.stroke.cap == LineCap::Round && open) {
                            const Vec2 first = pts_sub.front();
                            const Vec2 last = pts_sub.back();
                            const f32 d0 = glm::length(p - first);
                            const f32 d1 = glm::length(p - last);
                            const f32 aa = 1.0f;
                            if (d0 <= radius + aa || d1 <= radius + aa) {
                                const f32 dist = std::min(d0, d1);
                                const f32 cov = (dist <= radius - aa) ? 1.0f : 1.0f - ((dist - (radius - aa)) / (2.0f * aa));
                                if (cov > 0.0f) {
                                    hit = true;
                                    pixel_color.a *= cov * opacity;
                                }
                            }
                        }

                        if (!hit && (path.stroke.join == LineJoin::Round || path.stroke.join == LineJoin::Miter)) {
                            const usize join_start = closed ? 0 : 1;
                            const usize join_end = closed ? unique_count : (unique_count > 0 ? unique_count - 1 : 0);
                            for (usize i = join_start; i < join_end; ++i) {
                                const Vec2 v = pts_sub[i];
                                const f32 join_radius = radius;
                                const f32 d = glm::length(p - v);
                                const f32 aa = 1.0f;
                                if (d <= join_radius + aa) {
                                    const f32 cov = (d <= join_radius - aa) ? 1.0f : 1.0f - ((d - (join_radius - aa)) / (2.0f * aa));
                                    if (cov > 0.0f) {
                                        hit = true;
                                        pixel_color.a *= cov * opacity;
                                        break;
                                    }
                                }
                            }
                        }

                        if (hit) {
                            break;
                        }
                    }

                    if (hit) {
                        row[x] = compositor::blend(pixel_color, row[x], BlendMode::Normal);
                        break;
                    }
                }
            }
        }
    }
}

void PathRasterizer::draw_path(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                               const Camera& camera, i32 width, i32 height) {
    (void)camera;
    (void)width;
    (void)height;
    ::chronon3d::renderer::draw_path(fb, node.shape.path, state.matrix, node.color, &state);
}

Color resolve_fill_color(const Fill& fill, Vec2 p, const raster::BBox& bbox, f32 opacity) {
    if (fill.type == FillType::Solid) {
        Color c = fill.solid.to_linear();
        c.a *= opacity;
        return c;
    }

    const f32 w = std::max(1.0f, static_cast<f32>(bbox.x1 - bbox.x0));
    const f32 h = std::max(1.0f, static_cast<f32>(bbox.y1 - bbox.y0));
    const Vec2 local{
        (p.x - static_cast<f32>(bbox.x0)) / w,
        (p.y - static_cast<f32>(bbox.y0)) / h
    };

    f32 t = 0.0f;
    if (fill.type == FillType::LinearGradient) {
        const Vec2 dir = fill.gradient.to - fill.gradient.from;
        const f32 len2 = glm::dot(dir, dir);
        if (len2 > kPathEpsilon) {
            const Vec2 d = local - fill.gradient.from;
            t = glm::dot(d, dir) / len2;
        }
    } else if (fill.type == FillType::ConicGradient) {
        const Vec2 d = local - fill.gradient.from;
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
        const Vec2 d = local - fill.gradient.from;
        const Vec2 rv = fill.gradient.to - fill.gradient.from;
        const f32 r = glm::length(rv);
        t = (r > kPathEpsilon) ? glm::length(d) / r : 0.0f;
    }

    t = std::clamp(t, 0.0f, 1.0f);
    Color c = sample_gradient(fill.gradient, t);
    c.a *= opacity;
    return c;
}

} // namespace chronon3d::renderer
