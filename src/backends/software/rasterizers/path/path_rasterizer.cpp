#include <chronon3d/backends/software/rasterizers/path_rasterizer.hpp>
#include "path_rasterizer.hpp"

// Sub-module headers
#include "path_cache.hpp"
#include "path_stroke.hpp"
#include "path_utils.hpp"
#include "pip.hpp"

#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <glm/glm.hpp>

// PR4: BL2 fill orchestration.  Only linked when CHRONON3D_USE_BLEND2D
// is ON (see src/backends/software/blend2d/CMakeLists.txt); when the
// option is OFF this header is not on the include path so the legacy
// build keeps its Custom-only default semantics.  The CMake target
// adds `-Isrc/backends/software/blend2d`, so the include is bare.
#include "blend2d_path_filler.hpp"
// PR5: BL2 stroke orchestration (BLStrokeOptions + dash deferred to
// Chronon pre-raster).  Same gating by CHRONON3D_USE_BLEND2D.
#include "blend2d_path_stroker.hpp"

#include "../shape_rasterizer.hpp"

namespace chronon3d::renderer {

// ── PR4: vector rasterizer dispatch mode ─────────────────────────────
// Toggled at runtime via the env var
//   CHRONON3D_VECTOR_RASTERIZER_MODE ∈ {Custom, Blend2D, Compare}.
//
// Default is `Custom` (the legacy PIP-based loop) so every existing
// golden test stays pixel-exact.  `Blend2D` delegates the fill
// rasterisation to `rasterize_path_fill_blend2d(...)` (BL2 A8
// coverage + Chronon shade/composite).  `Compare` is reserved for the
// QA scaffold and currently aliases to `Blend2D`; a side-by-side
// diff harness arrives in a follow-up.
enum class VectorRasterizerDebugMode { Custom, Blend2D, Compare };

namespace {
[[nodiscard]] VectorRasterizerDebugMode current_vector_rasterizer_mode() {
    return VectorRasterizerDebugMode::Custom;
}
} // anonymous namespace

[[nodiscard]] Color resolve_compiled_gradient_color(
    const graphics::CompiledGradient& gradient, Vec2 p,
    const raster::BBox& bbox, f32 opacity)
{
    const f32 w = std::max(1.0f, static_cast<f32>(bbox.x1 - bbox.x0));
    const f32 h = std::max(1.0f, static_cast<f32>(bbox.y1 - bbox.y0));
    const Vec2 local{
        (p.x - static_cast<f32>(bbox.x0)) / w,
        (p.y - static_cast<f32>(bbox.y0)) / h};

    f32 t = 0.0f;
    if (gradient.type == graphics::GradientType::Linear) {
        const Vec2 direction = gradient.end - gradient.start;
        const f32 length_squared = glm::dot(direction, direction);
        if (length_squared > kPathEpsilon)
            t = glm::dot(local - gradient.start, direction) / length_squared;
    } else if (gradient.type == graphics::GradientType::Conic) {
        const Vec2 delta = local - gradient.center;
        f32 angle = std::atan2(delta.y, delta.x) - gradient.angle;
        constexpr f32 tau = 6.2831853071795864769f;
        angle -= tau * std::floor(angle / tau);
        t = angle / tau;
    } else {
        const f32 distance = glm::length(local - gradient.center);
        t = gradient.radius > kPathEpsilon ? distance / gradient.radius : 0.0f;
    }

    Color c = graphics::dither_gradient_sample(
        graphics::sample_compiled_gradient(gradient, t), p.x, p.y);
    c.a *= opacity;
    return c;
}

raster::BBox compute_path_bbox(const PathShape& path, const Mat4& model, f32 spread) {
    // PR2: source-of-truth now lives in path_geometry.hpp but stroke
    // rendering needs the legacy affine-only bbox with `+1.0f` slack,
    // so we keep the affine-specific loop here and pull the contours
    // through the unified SharedContours cache (no copy on hit).
    auto contours = flatten_path_geometry_cached(path);
    if (!contours || contours->empty()) return {0, 0, 0, 0};

    f32 min_x = 1e10f;
    f32 min_y = 1e10f;
    f32 max_x = -1e10f;
    f32 max_y = -1e10f;
    const f32 fallback_width = std::isfinite(path.stroke.width)
        ? std::max(0.0f, path.stroke.width) : 0.0f;
    const bool valid_brush = path.stroke.brush && path.stroke.brush->valid() &&
        std::isfinite(path.stroke.width) && path.stroke.width >= 0.0f;
    const f32 stroke_diameter = std::max(fallback_width,
        valid_brush ? path.stroke.brush->size : 0.0f);
    const f32 brush_extent = valid_brush
        ? path.stroke.brush->maximum_extent_radius(fallback_width)
        : std::max(0.0f, stroke_diameter * 0.5f);
    const f32 radius = brush_extent + spread + 1.0f;
    bool empty = true;

    for (const auto& contour : *contours) {
        for (const auto& p : contour.points) {
            const Vec2 s = transform_point(model, p);
            min_x = std::min(min_x, s.x - radius);
            min_y = std::min(min_y, s.y - radius);
            max_x = std::max(max_x, s.x + radius);
            max_y = std::max(max_y, s.y + radius);
            empty = false;
        }
    }

    if (empty) return {0, 0, 0, 0};

    return {
        static_cast<i32>(std::floor(min_x)),
        static_cast<i32>(std::floor(min_y)),
        static_cast<i32>(std::ceil(max_x)) + 1,
        static_cast<i32>(std::ceil(max_y)) + 1,
    };
}

void draw_path(Framebuffer& fb, const PathShape& path, const Mat4& model, const Color& stroke_color,
               const RenderState* state) {
    // PR2: pull flattened contours from the unified SharedContours
    // cache (no copy on cache hit).  The local screen_contours vector
    // below is the affine projection that the pixel loop iterates.
    auto contours = flatten_path_geometry_cached(path);
    if (!contours || contours->empty()) return;

    std::vector<PathContour> screen_contours;
    screen_contours.reserve(contours->size());
    for (const auto& contour : *contours) {
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

    const f32 radius = path.stroke.brush && path.stroke.brush->valid()
        ? path.stroke.brush->maximum_extent_radius(path.stroke.width)
        : std::max(0.0f, path.stroke.width * 0.5f);
    const f32 opacity = stroke_color.a;
    const bool fill_enabled = path.fill.enabled;

    // PR4: delegate fill rasterisation to BL2 when the debug mode is
    // active.  The custom per-pixel fill block below is skipped in that
    // case so the ROI isn't filled twice.
    const VectorRasterizerDebugMode vec_mode = current_vector_rasterizer_mode();
    if (fill_enabled && path.fill.type != FillType::Field2D &&
        path.fill.type != FillType::GradientMesh &&
        vec_mode != VectorRasterizerDebugMode::Custom) {
        rasterize_path_fill_blend2d(fb, path, model, opacity, bbox, state);
    }
    const bool use_custom_fill =
        fill_enabled && (vec_mode == VectorRasterizerDebugMode::Custom ||
                         path.fill.type == FillType::Field2D ||
                         path.fill.type == FillType::GradientMesh);

    std::optional<graphics::CompiledGradient> compiled_fill;
    if (use_custom_fill && path.fill.type != FillType::Solid &&
        path.fill.type != FillType::Field2D &&
        path.fill.type != FillType::GradientMesh) {
        compiled_fill = graphics::compile_gradient(path.fill.gradient);
    }

    // PR1: prepare trim + dash once outside the per-pixel loop.
    // Legacy code recomputed trim_polyline_points / dash_polyline_points
    // for every (x, y, contour) — O(W·H·N) wasted allocations.
    std::vector<detail::PreparedStrokeContour> prepared_strokes;
    if (path.stroke.enabled) {
        prepared_strokes = prepare_stroke_contours(screen_contours, path.stroke);
    }

    // PR5: in Blend2D mode hand the prepared contours to the BL2
    // stroker.  Cap / join / anti-aliased outline coverage come from
    // BL2; Chronon keeps colour resolution + alpha modulation.
    // Presence selects brush semantics even for invalid in-memory profiles:
    // invalid brush data must not silently fall back to a solid path stroke.
    const bool has_brush = path.stroke.brush.has_value();
    const bool use_custom_stroke =
        path.stroke.enabled && !has_brush && (vec_mode == VectorRasterizerDebugMode::Custom ||
            path.stroke.taper_start != 1.0f || path.stroke.taper_end != 1.0f);
    std::optional<graphics::CompiledGradient> compiled_stroke;
    if (use_custom_stroke && path.stroke.gradient.has_value()) {
        compiled_stroke = graphics::compile_gradient(*path.stroke.gradient);
    }
    if (path.stroke.enabled && !has_brush && vec_mode != VectorRasterizerDebugMode::Custom &&
        !use_custom_stroke) {
        rasterize_path_stroke_blend2d(
            fb, path, model, stroke_color,
            prepared_strokes.data(), prepared_strokes.size(),
            bbox, state);
    }

    // Brush dabs are rasterized into an isolated stroke layer so fill remains
    // beneath stroke regardless of whether the brush uses the analytic or dab path.
    const bool render_brush = path.stroke.enabled && has_brush && path.stroke.brush->valid() &&
        std::isfinite(path.stroke.width) && path.stroke.width >= 0.0f &&
        (path.stroke.brush->size > 0.0f || path.stroke.width > 0.0f);
    std::vector<Color> brush_layer;
    if (render_brush) {
        brush_layer.assign(static_cast<std::size_t>(bbox.x1 - bbox.x0) *
                           static_cast<std::size_t>(bbox.y1 - bbox.y0),
                           Color{0.0f, 0.0f, 0.0f, 0.0f});
    }
    if (render_brush) {
        const auto& brush = *path.stroke.brush;
        const float diameter = brush.size > 0.0f ? brush.size : path.stroke.width;
        const float nominal_radius = diameter * 0.5f;
        const auto& along_gradient = brush.along_path_gradient;
        if (!brush.requires_dabs()) {
            for (int y = bbox.y0; y < bbox.y1; ++y) {
                for (int x = bbox.x0; x < bbox.x1; ++x) {
                    if (state && !pixel_passes_mask(*state, x, y)) continue;
                    const Vec2 p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
                    float best_distance = std::numeric_limits<float>::infinity();
                    float best_progress = 0.0f;
                    for (const auto& contour : prepared_strokes) {
                        for (const auto& sub : contour.visible_subpaths) {
                            for (std::size_t segment = 1; segment < sub.points.size(); ++segment) {
                                const Vec2 a = sub.points[segment - 1];
                                const Vec2 delta = sub.points[segment] - a;
                                const float length_squared = glm::dot(delta, delta);
                                const float u = length_squared > 0.0f
                                    ? std::clamp(glm::dot(p - a, delta) / length_squared, 0.0f, 1.0f)
                                    : 0.0f;
                                const Vec2 nearest = a + delta * u;
                                const float distance = glm::length(p - nearest);
                                if (distance < best_distance) {
                                    best_distance = distance;
                                    const float segment_length = sub.cumulative_lengths[segment] -
                                        sub.cumulative_lengths[segment - 1];
                                    best_progress = sub.total_length > 0.0f
                                        ? (sub.cumulative_lengths[segment - 1] + u * segment_length) /
                                            sub.total_length : 0.0f;
                                }
                            }
                        }
                    }
                    const float taper = std::lerp(path.stroke.taper_start, path.stroke.taper_end, best_progress) *
                        graphics::BrushProfile::brush_taper(
                            best_progress, brush.taper_start, brush.taper_end);
                    const float pressure = brush.pressure_at(best_progress);
                    const float shape_alpha = brush.sample_alpha(best_distance /
                        std::max(1.0e-4f, nominal_radius * taper), best_progress, p, pressure);
                    if (shape_alpha <= 0.0f) continue;
                    Color paint = path.stroke.color.to_linear();
                    if (along_gradient) paint = graphics::sample_gradient(*along_gradient, best_progress);
                    else if (path.stroke.gradient) paint = graphics::sample_gradient(*path.stroke.gradient, best_progress);
                    paint.r *= stroke_color.r;
                    paint.g *= stroke_color.g;
                    paint.b *= stroke_color.b;
                    paint.a = std::clamp(shape_alpha * paint.a * opacity, 0.0f, 1.0f);
                    const std::size_t index = static_cast<std::size_t>(y - bbox.y0) *
                        static_cast<std::size_t>(bbox.x1 - bbox.x0) +
                        static_cast<std::size_t>(x - bbox.x0);
                    brush_layer[index] = compositor::blend(paint, brush_layer[index], BlendMode::Normal);
                    if (brush_layer[index].a > brush.opacity && brush_layer[index].a > 0.0f) {
                        const float scale = brush.opacity / brush_layer[index].a;
                        brush_layer[index].r *= scale;
                        brush_layer[index].g *= scale;
                        brush_layer[index].b *= scale;
                        brush_layer[index].a = brush.opacity;
                    }
                }
            }
        } else {
            for (const auto& contour : prepared_strokes) {
                for (const auto& sub : contour.visible_subpaths) {
                    if (sub.points.size() < 2 || sub.total_length <= kPathEpsilon)
                        continue;
                    const auto dabs = brush.sample_dabs(sub.points, sub.total_length, diameter);
                    for (const auto& dab : dabs) {
                        const float taper = std::lerp(
                            path.stroke.taper_start, path.stroke.taper_end, dab.progress) *
                            graphics::BrushProfile::brush_taper(
                                dab.progress, brush.taper_start, brush.taper_end);
                        const float dab_radius = nominal_radius * taper;
                        if (dab_radius <= 0.0f) continue;
                        const float minimum_width_factor = std::max(
                            0.5f, 1.0f - 0.5f * std::clamp(brush.width_jitter, 0.0f, 1.0f));
                        const float dab_extent = dab_radius *
                            (1.0f + brush.bleed_radius + brush.edge_noise * 0.1f) /
                            minimum_width_factor + 1.0f;
                        const int x0 = std::max(bbox.x0, static_cast<int>(
                            std::floor(dab.position.x - dab_extent)));
                        const int x1 = std::min(bbox.x1, static_cast<int>(
                            std::ceil(dab.position.x + dab_extent)));
                        const int y0 = std::max(bbox.y0, static_cast<int>(
                            std::floor(dab.position.y - dab_extent)));
                        const int y1 = std::min(bbox.y1, static_cast<int>(
                            std::ceil(dab.position.y + dab_extent)));
                        for (int y = y0; y < y1; ++y) {
                            for (int x = x0; x < x1; ++x) {
                                if (state && !pixel_passes_mask(*state, x, y)) continue;
                                const Vec2 pixel{static_cast<float>(x) + 0.5f,
                                                 static_cast<float>(y) + 0.5f};
                                const Vec2 offset = pixel - dab.position;
                                const float shape_alpha = brush.sample_dab_alpha(
                                    offset, dab_radius, dab.progress, dab.pressure, dab.angle, pixel);
                                if (shape_alpha <= 0.0f) continue;

                                float paint_progress = dab.progress;
                                float best_distance_squared = std::numeric_limits<float>::infinity();
                                for (std::size_t segment = 1; segment < sub.points.size(); ++segment) {
                                    const Vec2 a = sub.points[segment - 1];
                                    const Vec2 delta = sub.points[segment] - a;
                                    const float length_squared = glm::dot(delta, delta);
                                    const float u = length_squared > 0.0f
                                        ? std::clamp(glm::dot(pixel - a, delta) / length_squared,
                                                     0.0f, 1.0f)
                                        : 0.0f;
                                    const Vec2 nearest = a + delta * u;
                                    const Vec2 difference = pixel - nearest;
                                    const float distance_squared = glm::dot(difference, difference);
                                    if (distance_squared < best_distance_squared) {
                                        best_distance_squared = distance_squared;
                                        const float segment_length = sub.cumulative_lengths[segment] -
                                            sub.cumulative_lengths[segment - 1];
                                        paint_progress = (sub.cumulative_lengths[segment - 1] +
                                            u * segment_length) / sub.total_length;
                                    }
                                }

                                Color dab_color = path.stroke.color.to_linear();
                                if (along_gradient)
                                    dab_color = graphics::sample_gradient(*along_gradient, paint_progress);
                                else if (path.stroke.gradient)
                                    dab_color = graphics::sample_gradient(*path.stroke.gradient, paint_progress);
                                dab_color.r *= stroke_color.r;
                                dab_color.g *= stroke_color.g;
                                dab_color.b *= stroke_color.b;
                                const float alpha = std::clamp(
                                    shape_alpha * dab_color.a * opacity * brush.flow,
                                    0.0f, 1.0f);
                                if (alpha <= 0.0f) continue;

                                const std::size_t index = static_cast<std::size_t>(y - bbox.y0) *
                                    static_cast<std::size_t>(bbox.x1 - bbox.x0) +
                                    static_cast<std::size_t>(x - bbox.x0);
                                Color& destination = brush_layer[index];
                                const float inverse_alpha = 1.0f - alpha;
                                destination.r = dab_color.r * alpha + destination.r * inverse_alpha;
                                destination.g = dab_color.g * alpha + destination.g * inverse_alpha;
                                destination.b = dab_color.b * alpha + destination.b * inverse_alpha;
                                destination.a = alpha + destination.a * inverse_alpha;
                                if (destination.a > brush.opacity && destination.a > 0.0f) {
                                    const float scale = brush.opacity / destination.a;
                                    destination.r *= scale;
                                    destination.g *= scale;
                                    destination.b *= scale;
                                    destination.a = brush.opacity;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (i32 y = bbox.y0; y < bbox.y1; ++y) {
        Color* row = fb.pixels_row(y);
        for (i32 x = bbox.x0; x < bbox.x1; ++x) {
            if (state && !pixel_passes_mask(*state, x, y)) continue;

            const Vec2 p{static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f};

            if (use_custom_fill) {
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
                    Color pixel_color;
                    if (path.fill.type == FillType::Solid) {
                        pixel_color = path.fill.solid.to_linear();
                        pixel_color.a *= opacity;
                    } else if (path.fill.type == FillType::Field2D && path.fill.field) {
                        const f32 w = std::max(1.0f, static_cast<f32>(bbox.x1 - bbox.x0));
                        const f32 h = std::max(1.0f, static_cast<f32>(bbox.y1 - bbox.y0));
                        const Vec2 normalized{(p.x - bbox.x0) / w, (p.y - bbox.y0) / h};
                        const Color field_color = sample_fill_at(path.fill, normalized);
                        pixel_color = path.fill.field_ramp
                            ? field_color : field_color.to_linear();
                        pixel_color.a *= opacity;
                    } else if (path.fill.type == FillType::GradientMesh && path.fill.gradient_mesh) {
                        const f32 w = std::max(1.0f, static_cast<f32>(bbox.x1 - bbox.x0));
                        const f32 h = std::max(1.0f, static_cast<f32>(bbox.y1 - bbox.y0));
                        const Vec2 normalized{(p.x - bbox.x0) / w, (p.y - bbox.y0) / h};
                        pixel_color = path.fill.gradient_mesh->sample(normalized).to_linear();
                        pixel_color.a *= opacity;
                    } else {
                        pixel_color = resolve_compiled_gradient_color(
                            *compiled_fill, p, bbox, opacity);
                    }
                    row[x] = compositor::blend(pixel_color, row[x], BlendMode::Normal);
                    continue;
                }
            }

            if (use_custom_stroke) {
                bool hit = false;
                // Use gradient stroke colour when available.
                const Color base_color = [&]() -> Color {
                    if (path.stroke.gradient.has_value()) {
                        return resolve_compiled_gradient_color(
                            *compiled_stroke, p, bbox, 1.0f);
                    }
                    return path.stroke.color.to_linear();
                }();
                Color pixel_color{
                    stroke_color.r * base_color.r,
                    stroke_color.g * base_color.g,
                    stroke_color.b * base_color.b,
                    stroke_color.a * base_color.a
                };
                // PR1: iterate over pre-prepared stroke contours; trim + dash
                // already applied once during prepare_stroke_contours().
                for (const auto& stroke_contour : prepared_strokes) {
                    if (stroke_contour.visible_subpaths.empty()) continue;

                    for (const auto& sub : stroke_contour.visible_subpaths) {
                        const auto& pts_sub = sub.points;
                        if (pts_sub.size() < 2) continue;

                        const auto radius_at = [&](usize i) {
                            const f32 t = sub.total_length > kPathEpsilon
                                ? sub.cumulative_lengths[i] / sub.total_length : 0.0f;
                            return radius * std::lerp(path.stroke.taper_start, path.stroke.taper_end, t);
                        };

                        const bool closed = path.stroke.dash_array.empty() ? stroke_contour.source_closed : false;
                        const bool open = !closed;
                        const bool repeated_start = sub.repeated_start;
                        const usize unique_count = sub.unique_count;

                        for (usize i = 0; i + 1 < pts_sub.size(); ++i) {
                            Vec2 a = pts_sub[i];
                            Vec2 b = pts_sub[i + 1];
                            const f32 radius_a = radius_at(i);
                            const f32 radius_b = radius_at(i + 1);
                            if (path.stroke.cap == LineCap::Square && open && i == 0 && radius_a > 0.0f) {
                                const Vec2 dir = glm::normalize(b - a);
                                a -= dir * radius_a;
                            }
                            if (path.stroke.cap == LineCap::Square && open && i + 2 == pts_sub.size() && radius_b > 0.0f) {
                                const Vec2 dir = glm::normalize(b - a);
                                b += dir * radius_b;
                            }

                            const f32 cov = segment_coverage(p, a, b, radius_a, radius_b);
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
                            const f32 first_radius = radius_at(0);
                            const f32 last_radius = radius_at(pts_sub.size() - 1);
                            if (d0 <= first_radius + aa || d1 <= last_radius + aa) {
                                const bool first = d0 <= first_radius + aa;
                                const f32 dist = first ? d0 : d1;
                                const f32 cap_radius = first ? first_radius : last_radius;
                                const f32 cov = (dist <= cap_radius - aa) ? 1.0f : 1.0f - ((dist - (cap_radius - aa)) / (2.0f * aa));
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
                                const f32 join_radius = radius_at(i);
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

                        // Once any cap/join hit on this sub-path, exit
                        // the sub-path loop so subsequent sub-paths don't
                        // double-count pixel coverage.
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
    if (render_brush) {
        for (int y = bbox.y0; y < bbox.y1; ++y) {
            Color* row = fb.pixels_row(y);
            for (int x = bbox.x0; x < bbox.x1; ++x) {
                const std::size_t index = static_cast<std::size_t>(y - bbox.y0) *
                    static_cast<std::size_t>(bbox.x1 - bbox.x0) +
                    static_cast<std::size_t>(x - bbox.x0);
                if (brush_layer[index].a > 0.0f) {
                    const Color& source = brush_layer[index]; // premultiplied accumulation
                    Color& destination = row[x];             // framebuffer is premultiplied
                    const float source_alpha = std::clamp(source.a, 0.0f, 1.0f);
                    const float inverse_alpha = 1.0f - source_alpha;
                    destination.r = source.r + destination.r * inverse_alpha;
                    destination.g = source.g + destination.g * inverse_alpha;
                    destination.b = source.b + destination.b * inverse_alpha;
                    destination.a = source_alpha + destination.a * inverse_alpha;
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
    ::chronon3d::renderer::draw_path(fb, node.shape.path(), state.matrix, node.color, &state);
}

Color resolve_fill_color(const Fill& fill, Vec2 p, const raster::BBox& bbox, f32 opacity) {
    if (fill.type == FillType::Solid) {
        Color c = fill.solid.to_linear();
        c.a *= opacity;
        return c;
    }

    const f32 w = std::max(1.0f, static_cast<f32>(bbox.x1 - bbox.x0));
    const f32 h = std::max(1.0f, static_cast<f32>(bbox.y1 - bbox.y0));
    const Vec2 normalized{
        (p.x - static_cast<f32>(bbox.x0)) / w,
        (p.y - static_cast<f32>(bbox.y0)) / h};
    Color c;
    if (fill.type == FillType::Field2D) {
        c = sample_fill_at(fill, normalized);
    } else if (fill.type == FillType::GradientMesh && fill.gradient_mesh) {
        c = fill.gradient_mesh->sample(normalized).to_linear();
    } else if (fill.type == FillType::Solid) {
        c = fill.solid.to_linear();
    } else {
        c = graphics::dither_gradient_sample(
            sample_gradient_at(fill.gradient, normalized), p.x, p.y);
    }
    c.a *= opacity;
    return c;
}

} // namespace chronon3d::renderer
