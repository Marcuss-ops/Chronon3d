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
    const bool native_linear_gradient = node.fill.enabled &&
        node.fill.type == FillType::LinearGradient &&
        node.fill.gradient.type == graphics::GradientType::Linear &&
        node.fill.gradient.color_stops.size() >= 2 &&
        node.fill.gradient.color_stops.size() <= 3 &&
        node.fill.gradient.opacity_stops.empty() &&
        node.fill.gradient.spread == graphics::GradientSpread::Pad;
    const bool native_radial_gradient = node.fill.enabled &&
        node.fill.type == FillType::RadialGradient &&
        node.fill.gradient.type == graphics::GradientType::Radial &&
        node.fill.gradient.color_stops.size() >= 2 &&
        node.fill.gradient.color_stops.size() <= 3 &&
        node.fill.gradient.opacity_stops.empty() &&
        node.fill.gradient.spread == graphics::GradientSpread::Pad;
    const bool native_gradient = native_linear_gradient || native_radial_gradient;
    if ((type != ShapeType::Rect && type != ShapeType::RoundedRect &&
         type != ShapeType::Circle && type != ShapeType::Line) ||
        (type != ShapeType::Line && !native_gradient &&
         node.fill.type != FillType::Solid && node.fill.enabled) ||
        (native_gradient && (type == ShapeType::Circle || type == ShapeType::Line)) ||
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
    const bool fill_only_closed_shape = fill_only_circle ||
        type == ShapeType::Rect || type == ShapeType::RoundedRect;
    if (!is_line && ((!has_stroke && !fill_only_closed_shape) ||
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
    Color fill_premul = graph::native_promotion::premultiply(
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
    if (native_gradient) {
        if (has_stroke || graph::native_promotion::is_projected(state) ||
            !orthogonal_affine || has_mask) {
            return false;
        }
        const auto& gradient = node.fill.gradient;
        const auto project = [&state](Vec2 p, Vec2& result) {
            const Vec4 clip = state.matrix * Vec4(p, 0.0f, 1.0f);
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) ||
                !std::isfinite(clip.w) || std::abs(clip.w) < 1e-6f) return false;
            result = Vec2{clip.x / clip.w, clip.y / clip.w};
            return std::isfinite(result.x) && std::isfinite(result.y);
        };
        if (native_linear_gradient) {
            Vec2 start, end;
            if (!project(gradient.start * size, start) ||
                !project(gradient.end * size, end)) return false;
            line_params = Vec4{start.x, start.y, end.x, end.y};
        } else {
            Vec2 center;
            if (!project(gradient.center * size, center) ||
                !std::isfinite(gradient.radius) || gradient.radius <= 0.0f) return false;
            const float radius = std::max(size.x, size.y) * basis_x * gradient.radius;
            if (!std::isfinite(radius) || radius <= 0.0f) return false;
            line_params = Vec4{center.x, center.y, radius, 0.0f};
        }
        shape_params = Vec4{native_linear_gradient ? 6.0f : 9.0f, 0.0f, 0.0f,
                            static_cast<float>(gradient.color_stops.size())};
        std::array<float, 16> packed{};
        for (std::size_t i = 0; i < gradient.color_stops.size(); ++i) {
            const auto& stop = gradient.color_stops[i];
            if (!std::isfinite(stop.position) || stop.position < 0.0f ||
                stop.position > 1.0f || (i > 0 && stop.position < gradient.color_stops[i - 1].position))
                return false;
            bool stop_alpha_zero = false;
            const Color pm = graph::native_promotion::premultiply(
                stop.color, state.opacity, &stop_alpha_zero);
            const std::size_t base = i * 5;
            packed[base] = stop.position;
            packed[base + 1] = pm.r;
            packed[base + 2] = pm.g;
            packed[base + 3] = pm.b;
            packed[base + 4] = pm.a;
        }
        for (std::size_t i = 0; i < packed.size(); ++i)
            vertices[i / 2][i % 2] = packed[i];
        // Native shader kinds 6 and 9 use rounded-rect SDF with one uniform radius.
        if (type == ShapeType::RoundedRect) {
            shape_params.y = node.shape.rounded_rect().radius * basis_x;
        } else if (node.corner_radius > 0.0f) {
            shape_params.y = node.corner_radius * basis_x;
        }
        fill_premul = Color{0.0f, 0.0f, 0.0f, 0.0f};
        fill_alpha_zero = false;
    }
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
        // In the stroke ABI shape.w is the mask-present flag, not the
        // bottom corner radius used by the fill-only ABI.
        shape_params.w = 0.0f;
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

} // namespace chronon3d::backends::vulkan::native_shape_dispatch_detail
