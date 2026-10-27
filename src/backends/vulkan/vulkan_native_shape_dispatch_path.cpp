#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include "vulkan_native_shape_dispatch_detail.hpp"
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

namespace chronon3d::backends::vulkan::native_shape_dispatch_detail {
namespace {
std::vector<Vec2> simplify_flattened_contour(const std::vector<Vec2>& contour) {
    std::vector<Vec2> result;
    result.reserve(contour.size());
    for (const Vec2 point : contour) {
        if (result.empty() || glm::length(point - result.back()) > 1e-4f)
            result.push_back(point);
    }
    if (result.size() > 1 && glm::length(result.front() - result.back()) <= 1e-4f)
        result.pop_back();
    return result;
}
} // namespace
bool try_native_path_fill(
    VulkanBackend& backend, Framebuffer& framebuffer,
    const RenderNode& node, const RenderState& state) {
    if (node.shape.type() != ShapeType::Path ||
        framebuffer.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    const auto& path = node.shape.path();
    if (std::getenv("CHRONON3D_PATH_DIAG"))
        spdlog::info("[native-path-fill-diag] node={} fill={} fill_type={} stroke={} brush={} commands={} operators={} projected={}",
            node.name, path.fill.enabled, static_cast<int>(path.fill.type),
            path.stroke.enabled, path.stroke.brush.has_value(), path.commands.size(),
            path.operators.size(), graph::native_promotion::is_projected(state));
    if (path.stroke.brush.has_value()) return false;
    if (path.stroke.taper_start != 1.0f || path.stroke.taper_end != 1.0f)
        return false;
    if (!path.fill.enabled || path.fill.type != FillType::Solid ||
        (path.stroke.enabled && (path.stroke.width <= 0.0f ||
            path.stroke.trim_start != 0.0f || path.stroke.trim_end != 1.0f ||
            path.stroke.gradient.has_value() || path.stroke.cap != LineCap::Butt)) ||
        path.commands.empty() || graph::native_promotion::has_active_mask(state)) {
        return false;
    }
    const auto contours = math::flatten_path(path);
    if (contours.size() != 1) return false;
    auto polygon = simplify_flattened_contour(contours.front());
    if (polygon.size() < 3 || polygon.size() > 8)
        return false;
    std::vector<Vec2> vertices = std::move(polygon);
    const auto transform = [&state](Vec2 point, Vec2& out) {
        const Vec4 projected = state.matrix * Vec4(point, 0.0f, 1.0f);
        if (!std::isfinite(projected.w) || std::abs(projected.w) < 1e-6f) return false;
        out = Vec2{projected.x / projected.w, projected.y / projected.w};
        return std::isfinite(out.x) && std::isfinite(out.y);
    };
    for (auto& vertex : vertices) {
        Vec2 result;
        if (!transform(vertex, result)) return false;
        vertex = result;
    }

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
    const RenderNode& node, const RenderState& state);







bool try_native_path_stroke(
    VulkanBackend& backend, Framebuffer& framebuffer,
    const RenderNode& node, const RenderState& state) {
    if (node.shape.type() != ShapeType::Path ||
        framebuffer.surface_handle() == runtime::kInvalidRenderSurfaceHandle)
        return false;
    const auto& path = node.shape.path();
    if (std::getenv("CHRONON3D_PATH_DIAG"))
        spdlog::info("[native-path-stroke-entry] node={} stroke={} brush={} width={} cap={} fill={} fill_type={} operators={} projected={}",
            node.name, path.stroke.enabled, path.stroke.brush.has_value(), path.stroke.width,
            static_cast<int>(path.stroke.cap), path.fill.enabled, static_cast<int>(path.fill.type),
            path.operators.size(), graph::native_promotion::is_projected(state));
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
    if (!path.stroke.enabled || path.stroke.width <= 0.0f ||
        path.stroke.gradient.has_value() ||
        (path.stroke.cap != LineCap::Butt && path.stroke.cap != LineCap::Round) ||
        graph::native_promotion::has_active_mask(state))
        return false;

    const auto transform = [&state](Vec2 point) {
        const Vec4 p = state.matrix * Vec4(point, 0.0f, 1.0f);
        if (!std::isfinite(p.w) || std::abs(p.w) < 1e-6f)
            return Vec2{std::numeric_limits<float>::quiet_NaN()};
        return Vec2{p.x / p.w, p.y / p.w};
    };
    const float width = path.stroke.width;
    bool alpha_zero = false;
    const Color color = graph::native_promotion::premultiply(
        path.stroke.color, state.opacity, &alpha_zero);
    if (alpha_zero) return true;

    const auto contours = math::flatten_path(path);
    if (contours.empty()) return false;
    if (path_diag) {
        std::size_t flattened_points = 0;
        for (const auto& contour : contours) flattened_points += contour.size();
        spdlog::info("[native-path-geometry-diag] node={} contours={} points={} width={} fill_visible={}",
            node.name, contours.size(), flattened_points, width, visible_solid_fill);
    }

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
        if (contours.size() != 1) return false;
        const auto polygon = simplify_flattened_contour(contours.front());
        if (polygon.size() < 3 || polygon.size() > fill_vertices.size()) return false;
        fill_vertex_count = static_cast<std::uint32_t>(polygon.size());
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            fill_vertices[i] = transform(polygon[i]);
            if (!std::isfinite(fill_vertices[i].x) || !std::isfinite(fill_vertices[i].y))
                return false;
        }
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

    if (path_diag) {
        std::size_t max_run_points = 0;
        for (const auto run_length : run_lengths)
            max_run_points = std::max(max_run_points, static_cast<std::size_t>(run_length));
        spdlog::info("[native-path-dispatch-diag] node={} runs={} points={} max_run_points={} rect={}x{} work_items={}",
            node.name, run_lengths.size(), points.size(), max_run_points,
            x1 - x0, y1 - y0,
            static_cast<std::uint64_t>(std::max(0, x1 - x0)) *
                static_cast<std::uint64_t>(std::max(0, y1 - y0)));
    }

    // The shader selects the first covered run per pixel and blends once;
    // overlapping curve joins and translucent dashes therefore retain the
    // software rasterizer's first-hit/source-over behavior.
    return backend.fill_dashed_path_surface(
        framebuffer.surface_handle(), x0, y0, x1, y1, width, color, points,
        run_lengths, has_fill, fill_vertex_count, fill_vertices,
        path.stroke.cap == LineCap::Round).ok();
}
} // namespace chronon3d::backends::vulkan::native_shape_dispatch_detail
