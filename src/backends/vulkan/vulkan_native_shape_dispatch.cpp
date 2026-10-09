#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/cache/framebuffer_copy.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include "../../render_graph/nodes/detail/native_promotion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <glm/gtc/matrix_inverse.hpp>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::backends::vulkan {

namespace {

bool try_native_path_stroke(
    VulkanBackend& backend, Framebuffer& framebuffer,
    const RenderNode& node, const RenderState& state);

bool try_native_path_fill(
    VulkanBackend& backend, Framebuffer& framebuffer,
    const RenderNode& node, const RenderState& state) {
    if (node.shape.type() != ShapeType::Path ||
        framebuffer.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    const auto& path = node.shape.path();
    if (path.stroke.brush.has_value()) return false;
    if (path.stroke.taper_start != 1.0f || path.stroke.taper_end != 1.0f)
        return false;
    if (!path.fill.enabled || path.fill.type != FillType::Solid ||
        (path.stroke.enabled && (path.stroke.dash_array.empty() ||
            path.stroke.width <= 0.0f || path.stroke.trim_start != 0.0f ||
            path.stroke.trim_end != 1.0f || path.stroke.gradient.has_value() ||
            path.stroke.cap != LineCap::Butt)) || path.commands.empty() ||
        graph::native_promotion::has_active_mask(state) ||
        graph::native_promotion::is_projected(state)) {
        return false;
    }
    // fill_path_surface writes the solid polygon directly. Until it is
    // source-over composited, only opaque fill content can be promoted.
    if (path.fill.solid.a < 1.0f || state.opacity < 1.0f) return false;

    std::vector<Vec2> vertices;
    vertices.reserve(8);
    Vec2 current{0.0f};
    bool have_current = false;
    bool closed = false;
    bool valid = true;
    for (const auto& command : path.commands) {
        switch (command.type) {
        case PathCommandType::MoveTo:
            if (have_current || closed) valid = false;
            current = command.p0;
            if (vertices.size() >= 8) valid = false;
            else vertices.push_back(current);
            have_current = true;
            break;
        case PathCommandType::LineTo:
            if (!have_current || closed || vertices.size() >= 8) {
                valid = false;
                break;
            }
            vertices.push_back(command.p0);
            current = command.p0;
            break;
        case PathCommandType::Close:
            if (!have_current || vertices.size() < 3 || closed) valid = false;
            closed = true;
            break;
        case PathCommandType::CubicTo:
        case PathCommandType::QuadraticTo:
            valid = false;
            break;
        }
    }
    if (!valid || !closed || vertices.size() < 3 || vertices.size() > 8) return false;

    if (path.stroke.enabled && !path.stroke.dash_array.empty()) {
        if (path.stroke.width <= 0.0f || path.stroke.cap != LineCap::Butt ||
            path.stroke.trim_start != 0.0f || path.stroke.trim_end != 1.0f ||
            path.stroke.gradient.has_value() || !std::isfinite(path.stroke.dash_offset))
            return false;
        float pattern_length = 0.0f;
        for (const float length : path.stroke.dash_array) {
            if (!std::isfinite(length) || length < 0.0f) return false;
            pattern_length += length;
        }
        if (path.stroke.dash_array.size() % 2U != 0U) pattern_length *= 2.0f;
        if (!std::isfinite(pattern_length) || pattern_length <= 1.0e-5f ||
            !std::isfinite(path.stroke.dash_offset)) return false;
    }

    const auto transform = [&state](Vec2 point) {
        const Vec4 transformed = state.matrix * Vec4(point, 0.0f, 1.0f);
        return Vec2{transformed.x, transformed.y};
    };
    for (auto& vertex : vertices) vertex = transform(vertex);

    float min_x = vertices.front().x;
    float min_y = vertices.front().y;
    float max_x = min_x;
    float max_y = min_y;
    for (const auto& vertex : vertices) {
        min_x = std::min(min_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_x = std::max(max_x, vertex.x);
        max_y = std::max(max_y, vertex.y);
    }
    auto x0 = static_cast<std::int32_t>(std::floor(min_x));
    auto y0 = static_cast<std::int32_t>(std::floor(min_y));
    auto x1 = static_cast<std::int32_t>(std::ceil(max_x));
    auto y1 = static_cast<std::int32_t>(std::ceil(max_y));
    if (!graph::native_promotion::intersect_clip(
            state.clip_rect, framebuffer.width(), framebuffer.height(),
            x0, y0, x1, y1)) {
        return true;  // empty intersection: nothing to fill
    }

    bool alpha_zero = false;
    const Color color = graph::native_promotion::premultiply(
        path.fill.solid, state.opacity, &alpha_zero);
    if (alpha_zero)
        return !path.stroke.enabled ||
            try_native_path_stroke(backend, framebuffer, node, state);
    if (!backend.fill_path_surface(framebuffer.surface_handle(), vertices, color).ok())
        return false;
    return !path.stroke.enabled ||
        try_native_path_stroke(backend, framebuffer, node, state);
}

bool append_dash_runs(
    const std::vector<Vec2>& points, const std::vector<float>& pattern,
    float pattern_length, float dash_offset,
    std::vector<std::vector<Vec2>>& output) {
    if (points.size() < 2) return true;
    std::vector<float> cumulative{0.0f};
    cumulative.reserve(points.size());
    for (std::size_t i = 1; i < points.size(); ++i) {
        cumulative.push_back(cumulative.back() + glm::length(points[i] - points[i - 1]));
    }
    const float length = cumulative.back();
    if (!std::isfinite(length)) return false;
    if (length <= 1.0e-5f) return true;
    if (pattern_length <= 1.0e-5f) {
        output.push_back(points);
        return true;
    }

    const auto point_at = [&](float distance) {
        distance = std::clamp(distance, 0.0f, length);
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            const float a = cumulative[i], b = cumulative[i + 1];
            if (distance <= b || i + 2 == points.size()) {
                const float segment_length = b - a;
                const float t = segment_length > 1.0e-5f
                    ? (distance - a) / segment_length : 0.0f;
                return points[i] + (points[i + 1] - points[i]) *
                    std::clamp(t, 0.0f, 1.0f);
            }
        }
        return points.back();
    };

    const float offset = std::fmod(dash_offset, pattern_length);
    float cursor = -(offset < 0.0f ? offset + pattern_length : offset);
    std::size_t pattern_index = 0;
    bool on = true;
    while (cursor < length) {
        const float next = cursor + pattern[pattern_index];
        if (on) {
            const float begin = std::max(0.0f, cursor);
            const float end = std::min(length, next);
            if (end > begin + 1.0e-5f) {
                std::vector<Vec2> run{point_at(begin)};
                for (std::size_t i = 1; i + 1 < points.size(); ++i) {
                    if (cumulative[i] > begin + 1.0e-5f &&
                        cumulative[i] < end - 1.0e-5f) run.push_back(points[i]);
                }
                run.push_back(point_at(end));
                if (run.size() < 2) return false;
                output.push_back(std::move(run));
            }
        }
        cursor = next;
        on = !on;
        pattern_index = (pattern_index + 1U) % pattern.size();
    }
    return true;
}

bool try_native_path_stroke(
    VulkanBackend& backend, Framebuffer& framebuffer,
    const RenderNode& node, const RenderState& state) {
    if (node.shape.type() != ShapeType::Path ||
        framebuffer.surface_handle() == runtime::kInvalidRenderSurfaceHandle)
        return false;
    const auto& path = node.shape.path();
    // Brush profiles currently have software semantics only; never promote
    // them to the native Vulkan stroke path until pixel parity is certified.
    if (path.stroke.brush.has_value()) return false;
    const bool path_diag = [] {
        const char* value = std::getenv("CHRONON3D_PATH_DIAG");
        return value && std::string_view(value) == "1";
    }();
    if (path_diag) {
        spdlog::info("[native-path-diag] node={} fill={} fill_type={} fill_alpha={} stroke={} width={} cap={} trim={}:{} dash={} gradient={} projected={}",
            node.name, path.fill.enabled, static_cast<int>(path.fill.type),
            path.fill.solid.a, path.stroke.enabled, path.stroke.width, static_cast<int>(path.stroke.cap),
            path.stroke.trim_start, path.stroke.trim_end, path.stroke.dash_array.size(),
            path.stroke.gradient.has_value(), graph::native_promotion::is_projected(state));
    }
    const bool visible_solid_fill = path.fill.enabled &&
        path.fill.type == FillType::Solid && path.fill.solid.a > 0.0f;
    if ((visible_solid_fill && (path.stroke.dash_array.empty() ||
                                path.fill.type != FillType::Solid)) ||
        !path.stroke.enabled || path.stroke.width <= 0.0f ||
        path.stroke.gradient.has_value() || path.stroke.cap != LineCap::Butt ||
        graph::native_promotion::has_active_mask(state) ||
        graph::native_promotion::is_projected(state))
        return false;

    const auto transform = [&state](Vec2 point) {
        const Vec4 p = state.matrix * Vec4(point, 0.0f, 1.0f);
        return Vec2{p.x, p.y};
    };
    const float width = path.stroke.width;
    bool alpha_zero = false;
    const Color color = graph::native_promotion::premultiply(
        path.stroke.color, state.opacity, &alpha_zero);
    if (alpha_zero) return true;

    const auto contours = math::flatten_path(path);
    if (contours.empty()) return false;

    const bool full_trim = path.stroke.trim_start == 0.0f &&
                           path.stroke.trim_end == 1.0f;
    if (path.stroke.dash_array.empty() && full_trim &&
        path.stroke.color.a >= 1.0f && state.opacity >= 1.0f &&
        !path.fill.enabled && contours.size() == 1 && contours.front().size() == 2) {
        const Vec2 a = transform(contours.front()[0]);
        const Vec2 b = transform(contours.front()[1]);
        if (!std::isfinite(a.x) || !std::isfinite(a.y) ||
            !std::isfinite(b.x) || !std::isfinite(b.y)) return false;
        const float extent = width * 0.5f + 1.0f;
        auto x0 = static_cast<std::int32_t>(std::floor(std::min(a.x, b.x) - extent));
        auto y0 = static_cast<std::int32_t>(std::floor(std::min(a.y, b.y) - extent));
        auto x1 = static_cast<std::int32_t>(std::ceil(std::max(a.x, b.x) + extent));
        auto y1 = static_cast<std::int32_t>(std::ceil(std::max(a.y, b.y) + extent));
        if (!graph::native_promotion::intersect_clip(
                state.clip_rect, framebuffer.width(), framebuffer.height(),
                x0, y0, x1, y1)) return true;
        return backend.fill_solid_shape_surface(
            framebuffer.surface_handle(), x0, y0, x1, y1,
            Vec4{3.0f, width, 1.0f, 0.0f}, Vec4{a.x, a.y, b.x, b.y},
            color).ok();
    }

    std::vector<float> pattern = path.stroke.dash_array;
    float pattern_length = 0.0f;
    for (const float dash : pattern) {
        if (!std::isfinite(dash) || dash < 0.0f) return false;
        pattern_length += dash;
    }
    if (pattern.size() % 2U != 0U) {
        pattern.insert(pattern.end(), path.stroke.dash_array.begin(),
                       path.stroke.dash_array.end());
        pattern_length *= 2.0f;
    }
    if (!std::isfinite(pattern_length) ||
        !std::isfinite(path.stroke.dash_offset)) return false;

    std::array<Vec2, 8> fill_vertices{};
    std::uint32_t fill_vertex_count = 0;
    const bool has_fill = visible_solid_fill;
    if (has_fill) {
        if (pattern_length <= 1.0e-5f) return false;
        std::vector<Vec2> polygon;
        bool have_move = false, closed = false;
        for (const auto& command : path.commands) {
            switch (command.type) {
            case PathCommandType::MoveTo:
                if (have_move || closed || polygon.size() >= fill_vertices.size()) return false;
                polygon.push_back(command.p0);
                have_move = true;
                break;
            case PathCommandType::LineTo:
                if (!have_move || closed || polygon.size() >= fill_vertices.size()) return false;
                polygon.push_back(command.p0);
                break;
            case PathCommandType::Close:
                if (!have_move || closed || polygon.size() < 3) return false;
                closed = true;
                break;
            case PathCommandType::CubicTo:
            case PathCommandType::QuadraticTo:
                return false;
            }
        }
        if (!closed || contours.size() != 1) return false;
        fill_vertex_count = static_cast<std::uint32_t>(polygon.size());
        for (std::size_t i = 0; i < polygon.size(); ++i)
            fill_vertices[i] = transform(polygon[i]);
    }

    std::vector<std::vector<Vec2>> runs;
    for (const auto& local_contour : contours) {
        auto visible_contour = local_contour;
        if (!full_trim) {
            visible_contour = math::trim_polyline(
                local_contour, path.stroke.trim_start, path.stroke.trim_end);
            if (visible_contour.size() < 2) continue;
        }
        std::vector<Vec2> screen_contour;
        screen_contour.reserve(visible_contour.size());
        for (const Vec2 point : visible_contour) {
            const Vec2 p = transform(point);
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
            screen_contour.push_back(p);
        }
        if (pattern.empty()) {
            runs.push_back(std::move(screen_contour));
        } else if (!append_dash_runs(screen_contour, pattern, pattern_length,
                                     path.stroke.dash_offset, runs)) {
            return false;
        }
    }
    if (runs.empty()) return true;

    std::vector<Vec2> points;
    std::vector<std::uint32_t> run_lengths;
    std::size_t total_points = 0;
    for (const auto& run : runs) {
        if (run.size() > std::numeric_limits<std::uint32_t>::max() ||
            total_points > std::numeric_limits<std::size_t>::max() - run.size())
            return false;
        total_points += run.size();
    }
    points.reserve(total_points);
    run_lengths.reserve(runs.size());
    for (const auto& run : runs) {
        run_lengths.push_back(static_cast<std::uint32_t>(run.size()));
        points.insert(points.end(), run.begin(), run.end());
    }

    float min_x = points.front().x, min_y = points.front().y;
    float max_x = min_x, max_y = min_y;
    for (const Vec2 point : points) {
        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
    }
    const float extent = width * 0.5f + 1.0f;
    auto x0 = static_cast<std::int32_t>(std::floor(min_x - extent));
    auto y0 = static_cast<std::int32_t>(std::floor(min_y - extent));
    auto x1 = static_cast<std::int32_t>(std::ceil(max_x + extent));
    auto y1 = static_cast<std::int32_t>(std::ceil(max_y + extent));
    if (!graph::native_promotion::intersect_clip(
            state.clip_rect, framebuffer.width(), framebuffer.height(),
            x0, y0, x1, y1)) return true;

    // The shader selects the first covered run per pixel and blends once;
    // overlapping curve joins and translucent dashes therefore retain the
    // software rasterizer's first-hit/source-over behavior.
    return backend.fill_dashed_path_surface(
        framebuffer.surface_handle(), x0, y0, x1, y1, width, color, points,
        run_lengths, has_fill, fill_vertex_count, fill_vertices).ok();
}

bool try_native_grid(
    VulkanBackend& backend, Framebuffer& framebuffer,
    const RenderNode& node, const RenderState& state) {
    if (node.shape.type() != ShapeType::GridBackground ||
        graph::native_promotion::has_active_mask(state) ||
        graph::native_promotion::is_projected(state) ||
        framebuffer.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    const auto& grid = node.shape.grid_background();
    if (grid.size.x <= 0.0f || grid.size.y <= 0.0f) {
        return true;  // empty grid: nothing to draw (kernel no-op)
    }

    // The software kernel draws in canvas pixel coordinates anchored on
    // grid.size and ignores the node transform; the analytic GPU fill
    // reproduces exactly those semantics over the clip intersection.
    //
    // CPU/GPU parity: the software shape processor ALWAYS paints the full
    // viewport for GridBackgroundShape and intentionally ignores
    // state.clip_rect (a dirty-rect clip would chop grid lines at the
    // viewport edge).  This GPU promotion must mirror that: fill the whole
    // framebuffer, never the clip intersection.
    auto x0 = 0;
    auto y0 = 0;
    auto x1 = static_cast<std::int32_t>(framebuffer.width());
    auto y1 = static_cast<std::int32_t>(framebuffer.height());
    x0 = std::clamp(x0, 0, static_cast<std::int32_t>(framebuffer.width()));
    y0 = std::clamp(y0, 0, static_cast<std::int32_t>(framebuffer.height()));
    x1 = std::clamp(x1, x0, static_cast<std::int32_t>(framebuffer.width()));
    y1 = std::clamp(y1, y0, static_cast<std::int32_t>(framebuffer.height()));
    if (x0 >= x1 || y0 >= y1) return true;

    NativeGridFillParams params;
    params.size = grid.size;
    params.offset = grid.offset;
    params.bg_color = grid.bg_color;
    params.grid_color = grid.grid_color;
    params.spacing = grid.spacing;
    params.minor_thickness = grid.minor_thickness;
    params.major_thickness = grid.major_thickness;
    params.major_every = grid.major_every;
    params.centered = grid.centered;
    params.edge_fade = grid.edge_fade;
    params.major_glow = grid.major_glow;
    params.style = static_cast<std::uint32_t>(grid.style);
    params.opacity = state.opacity;
    return backend.fill_grid_surface(
        framebuffer.surface_handle(), x0, y0, x1, y1, params).ok();
}

bool try_native_solid_rect(
    VulkanBackend& backend, Framebuffer& framebuffer,
    const RenderNode& node, const RenderState& state) {
    const auto type = node.shape.type();
    if ((type != ShapeType::Rect && type != ShapeType::RoundedRect &&
         type != ShapeType::Circle && type != ShapeType::Line) ||
        (type != ShapeType::Line &&
         node.fill.type != FillType::Solid && node.fill.enabled) ||
        (graph::native_promotion::has_active_mask(state) &&
         (type == ShapeType::Line || (type != ShapeType::Circle &&
          type != ShapeType::Rect && type != ShapeType::RoundedRect))) ||
        framebuffer.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    // Gradients and masks remain unsupported. Closed-shape solid strokes are
    // accepted only for the exact SDF contract implemented below.
    const ShapeStroke* stroke_ptr = nullptr;
    if (type == ShapeType::Rect) stroke_ptr = &node.shape.rect().stroke;
    else if (type == ShapeType::RoundedRect) stroke_ptr = &node.shape.rounded_rect().stroke;
    else if (type == ShapeType::Circle) stroke_ptr = &node.shape.circle().stroke;
    bool has_stroke = stroke_ptr && stroke_ptr->enabled && stroke_ptr->width > 0.0f;
    Vec2 size{};
    Vec4 shape_params{0.0f};
    Vec4 line_params{0.0f};
    Color shape_color = node.color;
    const bool is_line = type == ShapeType::Line;
    if (type == ShapeType::Rect) {
        size = node.shape.rect().size;
        if (node.corner_radius > 0.0f) {
            shape_params = Vec4{1.0f, node.corner_radius, node.corner_radius, node.corner_radius};
            line_params = Vec4{node.corner_radius, 0.0f, 0.0f, 0.0f};
        }
    } else if (type == ShapeType::RoundedRect) {
        size = node.shape.rounded_rect().size;
        if (node.shape.rounded_rect().corner_radius) {
            const auto& r = *node.shape.rounded_rect().corner_radius;
            shape_params = Vec4{1.0f, r[0], r[1], r[2]};
            line_params = Vec4{r[3], 0.0f, 0.0f, 0.0f};
        } else {
            shape_params = Vec4{1.0f, node.shape.rounded_rect().radius,
                                node.shape.rounded_rect().radius, node.shape.rounded_rect().radius};
            line_params = Vec4{node.shape.rounded_rect().radius, 0.0f, 0.0f, 0.0f};
        }
    } else if (type == ShapeType::Circle) {
        size = Vec2{node.shape.circle().radius * 2.0f};
        shape_params = Vec4{2.0f, node.shape.circle().radius,
                            node.shape.circle().radius, 0.0f};
    } else {
        const auto& line = node.shape.line();
        if (!line.stroke.enabled || line.stroke.trim_start != 0.0f ||
            line.stroke.trim_end != 1.0f ||
            (std::abs(line.to.x) <= 1e-4f && std::abs(line.to.y) <= 1e-4f) ||
            line.stroke.width <= 0.0f) {
            return false;
        }
        const float width = std::max(line.thickness, line.stroke.width);
        shape_params = Vec4{3.0f, width, 0.0f, 0.0f};
        shape_color = line.stroke.color;
    }
    const bool fill_only_circle = type == ShapeType::Circle && !has_stroke;
    const bool has_mask = graph::native_promotion::has_active_mask(state);
    // Native mask coverage is currently supported only for closed solid strokes.
    // Fill-only primitives, paths and lines remain fail-closed under a mask.
    if (has_mask && !has_stroke) return false;
    // Solid strokes are evaluated by the shader in inverse-transformed local
    // coordinates (the same contract as the software hit test), so any
    // invertible affine transform, including shear, is exact. Fill-only shapes
    // keep the orthogonal-only contract, and projected transforms fail closed.
    const float affine_det = state.matrix[0][0] * state.matrix[1][1] -
                             state.matrix[0][1] * state.matrix[1][0];
    const bool invertible_affine = std::isfinite(affine_det) &&
                                   std::abs(affine_det) > 1e-6f;
    const float basis_x = std::hypot(state.matrix[0][0], state.matrix[0][1]);
    const float basis_y = std::hypot(state.matrix[1][0], state.matrix[1][1]);
    const float basis_dot = state.matrix[0][0] * state.matrix[1][0] +
                            state.matrix[0][1] * state.matrix[1][1];
    const bool orthogonal_affine = std::isfinite(basis_x) &&
        std::isfinite(basis_y) && std::isfinite(basis_dot) &&
        basis_x > 1e-6f && basis_y > 1e-6f &&
        std::abs(basis_dot) <= 1e-5f * basis_x * basis_y;
    const bool fill_circle_transform_supported = [&] {
        if (!fill_only_circle) return true;
        const bool uniform_scale = std::abs(basis_x - basis_y) <=
            1e-5f * std::max(basis_x, basis_y);
        const bool axis_aligned =
            (std::abs(state.matrix[0][1]) <= 1e-5f &&
             std::abs(state.matrix[1][0]) <= 1e-5f) ||
            (std::abs(state.matrix[0][0]) <= 1e-5f &&
             std::abs(state.matrix[1][1]) <= 1e-5f);
        return uniform_scale || axis_aligned;
    }();
    if (!is_line && ((!has_stroke && !fill_only_circle) ||
                     (!has_stroke && !orthogonal_affine) ||
                     (has_stroke && !invertible_affine) ||
                     !fill_circle_transform_supported ||
                     graph::native_promotion::is_projected(state))) {
        return false;
    }

    if (has_stroke) {
        const float sx = std::hypot(state.matrix[0][0], state.matrix[1][0]);
        const float sy = std::hypot(state.matrix[0][1], state.matrix[1][1]);
        // The software stroke contract is evaluated in inverse-transformed
        // local coordinates. The shader applies the full inverse orthogonal
        // transform, preserving the CPU SDF metric for scale and rotation.
        if (sx <= 1e-6f || sy <= 1e-6f)
            return false;
    }

    // Resolve the fill color exactly as the software shape processor does.
    bool fill_alpha_zero = false;
    const Color fill_premul = graph::native_promotion::premultiply(
        shape_color, state.opacity, &fill_alpha_zero);
    Color stroke_premul{0.0f, 0.0f, 0.0f, 0.0f};
    if (has_stroke) {
        bool stroke_alpha_zero = false;
        stroke_premul = graph::native_promotion::premultiply(
            stroke_ptr->color, state.opacity, &stroke_alpha_zero);
        if (stroke_alpha_zero) {
            if (fill_alpha_zero) return true;
            has_stroke = false;
        }
    } else if (fill_alpha_zero) {
        return true;
    }

    // Geometry bounds in destination pixels (axis-aligned affine outer hull).
    const Vec4 c00 = state.matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const Vec4 c10 = state.matrix * Vec4(size.x, 0.0f, 0.0f, 1.0f);
    const Vec4 c01 = state.matrix * Vec4(0.0f, size.y, 0.0f, 1.0f);
    const Vec4 c11 = state.matrix * Vec4(size.x, size.y, 0.0f, 1.0f);
    float geom_min_x = std::min({c00.x, c10.x, c01.x, c11.x});
    float geom_min_y = std::min({c00.y, c10.y, c01.y, c11.y});
    float geom_max_x = std::max({c00.x, c10.x, c01.x, c11.x});
    float geom_max_y = std::max({c00.y, c10.y, c01.y, c11.y});

    // The SDF kernel's fill-only circle branch consumes independent screen
    // radii. Preserve an axis-aligned non-uniform scale as an ellipse; a
    // uniformly scaled rotated circle remains a circle and needs no special
    // bounds correction.
    if (fill_only_circle) {
        shape_params.y = node.shape.circle().radius * basis_x;
        shape_params.z = node.shape.circle().radius * basis_y;
    }

    std::array<Vec2, 8> vertices{};
    if (has_stroke) {
        const float sx = std::hypot(state.matrix[0][0], state.matrix[1][0]);
        const float sy = std::hypot(state.matrix[0][1], state.matrix[1][1]);
        const float stroke_width_px = stroke_ptr->width * sx;
        if (!std::isfinite(stroke_width_px) || stroke_width_px <= 0.0f)
            return false;
        const auto alignment = stroke_ptr->alignment;
        glm::mat3 inverse_transform;
        inverse_transform[0][0] = state.matrix[0][0];
        inverse_transform[0][1] = state.matrix[0][1];
        inverse_transform[0][2] = state.matrix[0][3];
        inverse_transform[1][0] = state.matrix[1][0];
        inverse_transform[1][1] = state.matrix[1][1];
        inverse_transform[1][2] = state.matrix[1][3];
        inverse_transform[2][0] = state.matrix[3][0];
        inverse_transform[2][1] = state.matrix[3][1];
        inverse_transform[2][2] = state.matrix[3][3];
        inverse_transform = glm::inverse(inverse_transform);
        vertices[0] = size;
        vertices[1] = Vec2{inverse_transform[0][0], inverse_transform[0][1]};
        vertices[2] = Vec2{inverse_transform[1][0], inverse_transform[1][1]};
        vertices[3] = Vec2{inverse_transform[2][0], inverse_transform[2][1]};
        const float shader_alignment = alignment == StrokeAlignment::Inside ? 0.0f :
            alignment == StrokeAlignment::Center ? 1.0f : 2.0f;
        vertices[4] = Vec2{stroke_ptr->width, shader_alignment};
        vertices[7].y = 1.0f;
        if (type == ShapeType::RoundedRect &&
            node.shape.rounded_rect().corner_radius) {
            const auto& radii = *node.shape.rounded_rect().corner_radius;
            vertices[5] = Vec2{radii[0], radii[1]};
            vertices[6] = Vec2{radii[2], radii[3]};
            vertices[7].x = 1.0f;
        }
        line_params = Vec4{stroke_premul.r, stroke_premul.g,
                           stroke_premul.b, stroke_premul.a};
        const float extent_x = alignment == StrokeAlignment::Inside
            ? 0.0f : stroke_ptr->width * sx;
        const float extent_y = alignment == StrokeAlignment::Inside
            ? 0.0f : stroke_ptr->width * sy;
        if (extent_x > 0.0f || extent_y > 0.0f) {
            geom_min_x -= extent_x;
            geom_min_y -= extent_y;
            geom_max_x += extent_x;
            geom_max_y += extent_y;
        }
    }

    float dispatch_min_x, dispatch_min_y, dispatch_max_x, dispatch_max_y;

    if (is_line) {
        const Vec4 endpoint = state.matrix * Vec4(node.shape.line().to, 1.0f);
        line_params = Vec4{c00.x, c00.y, endpoint.x, endpoint.y};
        const float half_width = shape_params.y * 0.5f;
        dispatch_min_x = std::min(c00.x, endpoint.x) - half_width;
        dispatch_min_y = std::min(c00.y, endpoint.y) - half_width;
        dispatch_max_x = std::max(c00.x, endpoint.x) + half_width;
        dispatch_max_y = std::max(c00.y, endpoint.y) + half_width;
    } else {
        dispatch_min_x = geom_min_x;
        dispatch_min_y = geom_min_y;
        dispatch_max_x = geom_max_x;
        dispatch_max_y = geom_max_y;
    }

    auto x0 = static_cast<std::int32_t>(std::ceil(dispatch_min_x - 0.5f));
    auto y0 = static_cast<std::int32_t>(std::ceil(dispatch_min_y - 0.5f));
    auto x1 = static_cast<std::int32_t>(std::ceil(dispatch_max_x - 0.5f));
    auto y1 = static_cast<std::int32_t>(std::ceil(dispatch_max_y - 0.5f));
    x0 = std::clamp(x0, 0, framebuffer.width());
    y0 = std::clamp(y0, 0, framebuffer.height());
    x1 = std::clamp(x1, 0, framebuffer.width());
    y1 = std::clamp(y1, 0, framebuffer.height());
    if (state.clip_rect) {
        x0 = std::max(x0, state.clip_rect->x0);
        y0 = std::max(y0, state.clip_rect->y0);
        x1 = std::min(x1, state.clip_rect->x1);
        y1 = std::min(y1, state.clip_rect->y1);
    }
    if (x0 >= x1 || y0 >= y1) return true;

    std::vector<std::uint32_t> mask_words;
    if (has_mask) {
        ensure_mask_alpha_cache(state, framebuffer.width(), framebuffer.height());
        if (!state.mask_alpha_cache || x1 <= x0 || y1 <= y0) return false;
        const auto mask_width = static_cast<std::uint32_t>(x1 - x0);
        const auto mask_height = static_cast<std::uint32_t>(y1 - y0);
        const std::uint64_t coverage_count =
            static_cast<std::uint64_t>(mask_width) * mask_height;
        if (coverage_count > std::numeric_limits<std::size_t>::max() - 4U)
            return false;
        mask_words.resize(4U + static_cast<std::size_t>(coverage_count));
        mask_words[0] = static_cast<std::uint32_t>(x0);
        mask_words[1] = static_cast<std::uint32_t>(y0);
        mask_words[2] = mask_width;
        mask_words[3] = mask_height;
        std::size_t cursor = 4U;
        for (std::int32_t y = y0; y < y1; ++y) {
            for (std::int32_t x = x0; x < x1; ++x) {
                mask_words[cursor++] =
                    state.mask_alpha_cache->get_pixel(x, y).a > 0.5f ? 1U : 0U;
            }
        }
        shape_params.w = 1.0f;
    }

    return backend.fill_solid_shape_surface(
        framebuffer.surface_handle(), x0, y0, x1, y1,
        shape_params, line_params, fill_premul, vertices, mask_words).ok();
}

void sync_gpu_to_cpu(VulkanBackend& backend, Framebuffer& fb) {
    const auto handle = fb.surface_handle();
    if (handle == runtime::kInvalidRenderSurfaceHandle || !fb.is_gpu_authoritative()) {
        return;
    }
    const auto w = fb.width();
    const auto h = fb.height();
    if (w <= 0 || h <= 0) return;
    if (!fb.ensure_cpu_storage()) {
        throw std::runtime_error("Vulkan CPU fallback readback has no framebuffer storage");
    }

    if (fb.stride() == w) {
        std::span<float> dst(reinterpret_cast<float*>(fb.data()), static_cast<std::size_t>(w) * h * 4);
        const auto result = backend.download_surface(handle, dst);
        if (!result.ok()) {
            throw std::runtime_error("Vulkan CPU fallback readback failed: " + result.error().message);
        }
    } else {
        std::vector<float> packed(static_cast<std::size_t>(w) * h * 4);
        const auto result = backend.download_surface(handle, packed);
        if (!result.ok()) {
            throw std::runtime_error("Vulkan CPU fallback readback failed: " + result.error().message);
        }
        FramebufferCopyAuthority::copy_rows(
            fb.pixels_row(0), fb.stride(),
            reinterpret_cast<const Color*>(packed.data()), w, w, h);
    }
    fb.mark_cpu_gpu_synchronized();
}

void sync_cpu_to_gpu(VulkanBackend& backend, Framebuffer& fb) {
    const auto handle = fb.surface_handle();
    if (handle == runtime::kInvalidRenderSurfaceHandle) {
        return;
    }
    const auto w = fb.width();
    const auto h = fb.height();
    if (w <= 0 || h <= 0) return;

    const auto desc = runtime::SurfaceDesc::make(
        static_cast<std::uint32_t>(w),
        static_cast<std::uint32_t>(h),
        runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        runtime::LifetimeClass::FrameTransient);

    if (fb.stride() == w) {
        std::span<const float> src(reinterpret_cast<const float*>(fb.data()), static_cast<std::size_t>(w) * h * 4);
        const auto result = backend.upload_surface(handle, desc, src);
        if (!result.ok()) {
            throw std::runtime_error("Vulkan CPU fallback upload failed: " + result.error().message);
        }
    } else {
        std::vector<float> packed(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t y = 0; y < static_cast<std::size_t>(h); ++y) {
            const Color* row = fb.data() + y * static_cast<std::size_t>(fb.stride());
            std::memcpy(packed.data() + y * static_cast<std::size_t>(w) * 4, row, static_cast<std::size_t>(w) * sizeof(Color));
        }
        const auto result = backend.upload_surface(handle, desc, packed);
        if (!result.ok()) {
            throw std::runtime_error("Vulkan CPU fallback upload failed: " + result.error().message);
        }
    }
    fb.mark_gpu_authoritative();
}

} // namespace

void VulkanBackend::unsupported(const char* operation) {
    throw std::runtime_error(std::string{"VulkanBackend::"} + operation +
                             ": RenderSurface execution is not wired yet");
}

void VulkanBackend::apply_per_pixel_dof(
    Framebuffer& framebuffer, std::span<const float> depth,
    const DepthOfFieldSettings& dof, const LensModel& lens,
    const std::optional<raster::BBox>& clip) {
    // A CPU depth vector cannot be consumed by a device-local image kernel.
    // Never hide that contract violation behind an implicit GPU→CPU readback.
    (void)framebuffer; (void)depth; (void)dof; (void)lens; (void)clip;
    unsupported("apply_per_pixel_dof (native depth surface required)");
}

graph::RenderOpResult VulkanBackend::draw_node(Framebuffer& framebuffer, const RenderNode& node,
                                               const RenderState& state, const Camera& camera,
                                               int width, int height) {
    const auto completed_native_draw = [&framebuffer] {
        framebuffer.mark_gpu_authoritative();
        return graph::RenderOpResult(graph::RenderOpOutcome{1});
    };
    if (try_native_grid(*this, framebuffer, node, state)) return completed_native_draw();
    if (try_native_path_fill(*this, framebuffer, node, state)) return completed_native_draw();
    if (try_native_path_stroke(*this, framebuffer, node, state)) return completed_native_draw();
    if (try_native_solid_rect(*this, framebuffer, node, state)) return completed_native_draw();

    if (m_fallback_backend) {
        sync_gpu_to_cpu(*this, framebuffer);
        auto result = m_fallback_backend->draw_node(framebuffer, node, state, camera, width, height);
        if (!result.ok()) return result;
        sync_cpu_to_gpu(*this, framebuffer);
        return result;
    }

    (void)camera; (void)width; (void)height;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_node: no legacy-node fallback attached; node='" +
        std::string(node.name) + "' shape_type=" +
        std::to_string(static_cast<int>(node.shape.type()))));
}

void VulkanBackend::apply_effect_stack(
    Framebuffer& framebuffer, const EffectStack& stack,
    const effects::EffectExecutionContext& context) {
    if (m_fallback_backend) {
        sync_gpu_to_cpu(*this, framebuffer);
        m_fallback_backend->apply_effect_stack(framebuffer, stack, context);
        sync_cpu_to_gpu(*this, framebuffer);
        return;
    }
    (void)framebuffer; (void)stack; (void)context;
    unsupported("apply_effect_stack");
}

void VulkanBackend::composite_layer(Framebuffer& destination, const Framebuffer& source,
                                    BlendMode mode, const std::optional<raster::BBox>& clip,
                                    CompositeOperator op) {
    if (destination.surface_handle() == runtime::kInvalidRenderSurfaceHandle ||
        source.surface_handle() == runtime::kInvalidRenderSurfaceHandle ||
        mode != BlendMode::Normal || op != CompositeOperator::SourceOver ||
        destination.width() != source.width() || destination.height() != source.height()) {
        if (m_fallback_backend) {
            sync_gpu_to_cpu(*this, destination);
            sync_gpu_to_cpu(*this, const_cast<Framebuffer&>(source));
            m_fallback_backend->composite_layer(destination, source, mode, clip, op);
            sync_cpu_to_gpu(*this, destination);
            return;
        }
        if (destination.surface_handle() == runtime::kInvalidRenderSurfaceHandle ||
            source.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
            throw std::invalid_argument(
                "VulkanBackend::composite_layer: missing native surface handle "
                "(CPU blit fallback was demolished; materialize surfaces first)");
        }
        if (mode != BlendMode::Normal || op != CompositeOperator::SourceOver) {
            throw std::runtime_error("VulkanBackend::composite_layer: only Normal/SourceOver is implemented");
        }
        throw std::runtime_error("VulkanBackend::composite_layer: surface dimensions differ");
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    composite_legacy_surface(destination, source, mode, clip);
    destination.mark_gpu_authoritative();
#else
    unsupported("composite_layer");
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run(
    Framebuffer& framebuffer, const TextRunShape& shape,
    const glm::mat4& model_matrix, float opacity) {
    if (m_fallback_backend) {
        sync_gpu_to_cpu(*this, framebuffer);
        auto result = m_fallback_backend->draw_text_run(framebuffer, shape, model_matrix, opacity);
        if (!result.ok()) return result;
        sync_cpu_to_gpu(*this, framebuffer);
        return result;
    }
    (void)framebuffer; (void)shape; (void)model_matrix; (void)opacity;
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run: no legacy-node fallback attached"));
}

void VulkanBackend::apply_blur(
    Framebuffer& framebuffer, float radius,
    const std::optional<raster::BBox>& clip) {
    if (m_fallback_backend) {
        sync_gpu_to_cpu(*this, framebuffer);
        m_fallback_backend->apply_blur(framebuffer, radius, clip);
        sync_cpu_to_gpu(*this, framebuffer);
        return;
    }
    (void)framebuffer; (void)radius; (void)clip;
    unsupported("apply_blur");
}

} // namespace chronon3d::backends::vulkan
