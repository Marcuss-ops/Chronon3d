#include <chronon3d/backends/software/rasterizers/path_rasterizer.hpp>
#include "path_rasterizer.hpp"

#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "shape_rasterizer.hpp"

namespace chronon3d::renderer {

namespace {

constexpr f32 kEpsilon = 1e-6f;
using CacheKey = u64;

CacheKey hash_combine(CacheKey seed, CacheKey value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

template <typename T>
CacheKey hash_value(const T& value) {
    return static_cast<CacheKey>(std::hash<T>{}(value));
}

CacheKey hash_path(const PathShape& path) {
    CacheKey seed = hash_value(path.closed);
    seed = hash_combine(seed, hash_value(path.commands.size()));
    for (const auto& cmd : path.commands) {
        seed = hash_combine(seed, hash_value(static_cast<int>(cmd.type)));
        seed = hash_combine(seed, hash_value(cmd.p0.x));
        seed = hash_combine(seed, hash_value(cmd.p0.y));
        seed = hash_combine(seed, hash_value(cmd.p1.x));
        seed = hash_combine(seed, hash_value(cmd.p1.y));
        seed = hash_combine(seed, hash_value(cmd.p2.x));
        seed = hash_combine(seed, hash_value(cmd.p2.y));
    }
    return seed;
}

std::unordered_map<CacheKey, std::shared_ptr<const std::vector<PathContour>>> g_flatten_cache;
std::mutex g_flatten_cache_mutex;

Vec2 transform_point(const Mat4& model, Vec2 p) {
    const Vec4 v = model * Vec4{p.x, p.y, 0.0f, 1.0f};
    if (std::abs(v.w) < kEpsilon) {
        return {v.x, v.y};
    }
    return {v.x / v.w, v.y / v.w};
}

bool point_in_polygon_even_odd(Vec2 p, const std::vector<Vec2>& poly) {
    if (poly.size() < 3) return false;

    bool inside = false;
    for (usize i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        const bool crosses =
            ((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / ((b.y - a.y) + kEpsilon) + a.x);
        if (crosses) inside = !inside;
    }
    return inside;
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
        if (len2 > kEpsilon) {
            const Vec2 d = local - fill.gradient.from;
            t = glm::dot(d, dir) / len2;
        }
    } else {
        const Vec2 d = local - fill.gradient.from;
        const Vec2 rv = fill.gradient.to - fill.gradient.from;
        const f32 r = glm::length(rv);
        t = (r > kEpsilon) ? glm::length(d) / r : 0.0f;
    }

    t = std::clamp(t, 0.0f, 1.0f);
    Color c = sample_gradient(fill.gradient, t).to_linear();
    c.a *= opacity;
    return c;
}

f32 segment_coverage(Vec2 p, Vec2 a, Vec2 b, f32 radius) {
    const Vec2 ab = b - a;
    const f32 len_sq = glm::dot(ab, ab);
    if (len_sq < kEpsilon) {
        const f32 dist = glm::length(p - a);
        const f32 aa = 1.0f;
        if (dist <= radius - aa) return 1.0f;
        if (dist >= radius + aa) return 0.0f;
        return 1.0f - ((dist - (radius - aa)) / (2.0f * aa));
    }

    const f32 raw_t = glm::dot(p - a, ab) / len_sq;
    if (raw_t < 0.0f || raw_t > 1.0f) return 0.0f;

    const Vec2 closest = a + ab * raw_t;
    const f32 dist = glm::length(closest - p);
    const f32 aa = 1.0f;
    if (dist <= radius - aa) return 1.0f;
    if (dist >= radius + aa) return 0.0f;
    return 1.0f - ((dist - (radius - aa)) / (2.0f * aa));
}

std::vector<Vec2> trim_polyline_points(const std::vector<Vec2>& points, bool closed, f32 start_t, f32 end_t) {
    if (points.size() < 2) return {};
    if (end_t <= start_t) return {};

    std::vector<std::pair<Vec2, Vec2>> segments;
    segments.reserve(points.size());
    for (usize i = 0; i + 1 < points.size(); ++i) {
        segments.push_back({points[i], points[i + 1]});
    }
    if (closed && glm::length(points.front() - points.back()) > kEpsilon) {
        segments.push_back({points.back(), points.front()});
    }

    std::vector<f32> lengths;
    lengths.reserve(segments.size());
    f32 total = 0.0f;
    for (const auto& seg : segments) {
        const f32 len = glm::length(seg.second - seg.first);
        lengths.push_back(len);
        total += len;
    }
    if (total < kEpsilon) return {};

    const f32 start_d = std::clamp(start_t, 0.0f, 1.0f) * total;
    const f32 end_d = std::clamp(end_t, 0.0f, 1.0f) * total;
    if (end_d <= start_d) return {};

    std::vector<Vec2> out;
    f32 cursor = 0.0f;
    for (usize i = 0; i < segments.size(); ++i) {
        const auto& [a, b] = segments[i];
        const f32 len = lengths[i];
        if (len < kEpsilon) {
            cursor += len;
            continue;
        }

        const f32 seg_start = cursor;
        const f32 seg_end = cursor + len;
        cursor = seg_end;

        if (seg_end < start_d || seg_start > end_d) {
            continue;
        }

        const Vec2 dir = (b - a) / len;
        if (out.empty()) {
            const f32 t0 = std::clamp((start_d - seg_start) / len, 0.0f, 1.0f);
            out.push_back(a + dir * (t0 * len));
        }

        const f32 t1 = std::clamp((end_d - seg_start) / len, 0.0f, 1.0f);
        out.push_back(a + dir * (t1 * len));

        if (seg_end >= end_d) {
            break;
        }
    }

    return out;
}

std::vector<PathContour> flatten_to_contours(const PathShape& path) {
    const CacheKey key = hash_path(path);
    {
        std::lock_guard<std::mutex> lock(g_flatten_cache_mutex);
        auto it = g_flatten_cache.find(key);
        if (it != g_flatten_cache.end() && it->second) {
            return *it->second;
        }
    }

    std::vector<PathContour> contours;
    const auto subpaths = math::flatten_path(path);

    contours.reserve(subpaths.size());
    for (auto points : subpaths) {
        PathContour contour;
        contour.points = std::move(points);
        contour.closed = path.closed;
        if (!contour.points.empty()) {
            const bool already_closed =
                contour.points.size() > 2 &&
                glm::length(contour.points.front() - contour.points.back()) < 1e-4f;
            if (path.closed && !already_closed) {
                contour.points.push_back(contour.points.front());
            }
            contour.closed = contour.closed || already_closed;
        }
        contours.push_back(std::move(contour));
    }

    {
        std::lock_guard<std::mutex> lock(g_flatten_cache_mutex);
        g_flatten_cache.emplace(key, std::make_shared<const std::vector<PathContour>>(contours));
    }

    return contours;
}

} // namespace

std::vector<PathContour> flatten_path(const PathShape& path) {
    return flatten_to_contours(path);
}

raster::BBox compute_path_bbox(const PathShape& path, const Mat4& model, f32 spread) {
    const auto contours = flatten_to_contours(path);
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
    const auto contours = flatten_to_contours(path);
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

    const f32 radius = std::max(0.0f, path.stroke.width * 0.5f);
    const f32 opacity = stroke_color.a;
    const bool fill_enabled = true;

    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) continue;

            const Vec2 p{static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f};

            if (fill_enabled) {
                bool inside = false;
                for (const auto& contour : screen_contours) {
                    if (contour.closed && point_in_polygon_even_odd(p, contour.points)) {
                        inside = !inside;
                    }
                }
                if (inside) {
                    const Color pixel_color = resolve_fill_color(path.fill, p, bbox, opacity);
                    row[x] = compositor::blend(pixel_color, row[x], BlendMode::Normal);
                    continue;
                }
            }

            bool hit = false;
            Color pixel_color = stroke_color;
            for (const auto& contour : screen_contours) {
                if (contour.points.size() < 2) continue;

                const auto trimmed = trim_polyline_points(
                    contour.points,
                    contour.closed,
                    path.stroke.trim_start,
                    path.stroke.trim_end);
                const auto& pts = trimmed.empty() ? contour.points : trimmed;
                if (pts.size() < 2) continue;

                const bool closed = contour.closed;
                const bool open = !closed;
                const bool repeated_start = closed && pts.size() > 2 &&
                    glm::length(pts.front() - pts.back()) < 1e-4f;
                const usize unique_count = repeated_start ? pts.size() - 1 : pts.size();

                for (usize i = 0; i + 1 < pts.size(); ++i) {
                    Vec2 a = pts[i];
                    Vec2 b = pts[i + 1];
                    if (path.stroke.cap == LineCap::Square && open && i == 0) {
                        const Vec2 dir = glm::normalize(b - a);
                        a -= dir * radius;
                    }
                    if (path.stroke.cap == LineCap::Square && open && i + 2 == pts.size()) {
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
                    const Vec2 first = pts.front();
                    const Vec2 last = pts.back();
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
                        const Vec2 v = pts[i];
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
                    row[x] = compositor::blend(pixel_color, row[x], BlendMode::Normal);
                    break;
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
    ::chronon3d::renderer::draw_path(fb, node.shape.path, node.world_transform.to_matrix(), node.color, &state);
}

} // namespace chronon3d::renderer
