#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include "../../render_graph/nodes/detail/native_promotion.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace chronon3d::backends::vulkan {

namespace {

bool try_native_path_fill(
    VulkanBackend& backend, Framebuffer& framebuffer,
    const RenderNode& node, const RenderState& state) {
    if (node.shape.type() != ShapeType::Path ||
        framebuffer.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    const auto& path = node.shape.path();
    if (!path.fill.enabled || path.fill.type != FillType::Solid ||
        path.stroke.enabled || path.commands.empty() ||
        graph::native_promotion::has_active_mask(state) ||
        graph::native_promotion::is_projected(state)) {
        return false;
    }

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
    if (alpha_zero) return true;
    return backend.fill_path_surface(framebuffer.surface_handle(), vertices, color).ok();
}

bool try_native_path_stroke(
    VulkanBackend& backend, Framebuffer& framebuffer,
    const RenderNode& node, const RenderState& state) {
    if (node.shape.type() != ShapeType::Path ||
        framebuffer.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    const auto& path = node.shape.path();
    if (path.fill.enabled || !path.stroke.enabled || path.stroke.width <= 0.0f ||
        path.stroke.trim_start != 0.0f || path.stroke.trim_end != 1.0f ||
        !path.stroke.dash_array.empty() || path.stroke.gradient.has_value() ||
        path.stroke.cap != LineCap::Butt ||
        graph::native_promotion::has_active_mask(state) ||
        graph::native_promotion::is_projected(state)) {
        return false;
    }

    const float width = path.stroke.width;
    bool alpha_zero = false;
    const Color color = graph::native_promotion::premultiply(
        path.stroke.color, state.opacity, &alpha_zero);
    if (alpha_zero) return true;

    const auto transform = [&state](Vec2 point) {
        const Vec4 transformed = state.matrix * Vec4(point, 0.0f, 1.0f);
        return Vec2{transformed.x, transformed.y};
    };
    bool emitted = false;
    bool ok = true;
    Vec2 current{0.0f};
    Vec2 subpath_start{0.0f};
    bool have_current = false;

    const auto emit = [&](Vec2 from, Vec2 to) {
        if (!ok || glm::length(to - from) <= 1e-5f) return;
        const Vec2 a = transform(from);
        const Vec2 b = transform(to);
        const float half_width = width * 0.5f;
        auto x0 = static_cast<std::int32_t>(std::floor(std::min(a.x, b.x) - half_width));
        auto y0 = static_cast<std::int32_t>(std::floor(std::min(a.y, b.y) - half_width));
        auto x1 = static_cast<std::int32_t>(std::ceil(std::max(a.x, b.x) + half_width));
        auto y1 = static_cast<std::int32_t>(std::ceil(std::max(a.y, b.y) + half_width));
        x0 = std::clamp(x0, 0, framebuffer.width());
        y0 = std::clamp(y0, 0, framebuffer.height());
        x1 = std::clamp(x1, 0, framebuffer.width());
        y1 = std::clamp(y1, 0, framebuffer.height());
        if (x0 >= x1 || y0 >= y1) return;
        ok = backend.fill_solid_shape_surface(
            framebuffer.surface_handle(), x0, y0, x1, y1,
            Vec4{3.0f, width, 0.0f, 0.0f}, Vec4{a.x, a.y, b.x, b.y}, color).ok();
        emitted = true;
    };

    for (const auto& command : path.commands) {
        switch (command.type) {
        case PathCommandType::MoveTo:
            current = subpath_start = command.p0;
            have_current = true;
            break;
        case PathCommandType::LineTo:
            if (!have_current) break;
            emit(current, command.p0);
            current = command.p0;
            break;
        case PathCommandType::QuadraticTo: {
            if (!have_current) break;
            const Vec2 from = current;
            for (int i = 1; i <= 8; ++i) {
                const float t = static_cast<float>(i) / 8.0f;
                const float u = 1.0f - t;
                const Vec2 point = u * u * from + 2.0f * u * t * command.p1 + t * t * command.p0;
                emit(i == 1 ? from : current, point);
                current = point;
            }
            break;
        }
        case PathCommandType::CubicTo: {
            if (!have_current) break;
            const Vec2 from = current;
            for (int i = 1; i <= 12; ++i) {
                const float t = static_cast<float>(i) / 12.0f;
                const float u = 1.0f - t;
                const Vec2 point = u * u * u * from + 3.0f * u * u * t * command.p1 +
                    3.0f * u * t * t * command.p2 + t * t * t * command.p0;
                emit(i == 1 ? from : current, point);
                current = point;
            }
            break;
        }
        case PathCommandType::Close:
            if (have_current) {
                emit(current, subpath_start);
                current = subpath_start;
            }
            break;
        }
    }
    return emitted && ok;
}

bool try_native_solid_rect(
    VulkanBackend& backend, Framebuffer& framebuffer,
    const RenderNode& node, const RenderState& state) {
    const auto type = node.shape.type();
    if ((type != ShapeType::Rect && type != ShapeType::RoundedRect &&
         type != ShapeType::Circle && type != ShapeType::Line) ||
        (node.fill.type != FillType::Solid && node.fill.enabled) ||
        graph::native_promotion::has_active_mask(state) ||
        framebuffer.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    Vec2 size{};
    Vec4 shape_params{0.0f};
    Vec4 line_params{0.0f};
    Color shape_color = node.color;
    const bool is_line = type == ShapeType::Line;
    if (type == ShapeType::Rect) {
        if (node.shape.rect().stroke.enabled) return false;
        size = node.shape.rect().size;
        if (node.corner_radius > 0.0f) {
            shape_params = Vec4{1.0f, node.corner_radius, node.corner_radius, 0.0f};
        }
    } else if (type == ShapeType::RoundedRect) {
        if (node.shape.rounded_rect().stroke.enabled) return false;
        size = node.shape.rounded_rect().size;
        shape_params = Vec4{1.0f, node.shape.rounded_rect().radius,
                            node.shape.rounded_rect().radius, 0.0f};
    } else if (type == ShapeType::Circle) {
        if (node.shape.circle().stroke.enabled) return false;
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
    if (!is_line && !graph::native_promotion::is_axis_aligned_affine(state)) {
        return false;
    }

    bool alpha_zero = false;
    const Color color = graph::native_promotion::premultiply(shape_color, state.opacity,
                                                      &alpha_zero);
    if (alpha_zero) return true;

    const Vec4 c00 = state.matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const Vec4 c10 = state.matrix * Vec4(size.x, 0.0f, 0.0f, 1.0f);
    const Vec4 c01 = state.matrix * Vec4(0.0f, size.y, 0.0f, 1.0f);
    const Vec4 c11 = state.matrix * Vec4(size.x, size.y, 0.0f, 1.0f);
    float min_x = std::min({c00.x, c10.x, c01.x, c11.x});
    float min_y = std::min({c00.y, c10.y, c01.y, c11.y});
    float max_x = std::max({c00.x, c10.x, c01.x, c11.x});
    float max_y = std::max({c00.y, c10.y, c01.y, c11.y});
    if (is_line) {
        const Vec4 endpoint = state.matrix * Vec4(node.shape.line().to, 1.0f);
        line_params = Vec4{c00.x, c00.y, endpoint.x, endpoint.y};
        const float half_width = shape_params.y * 0.5f;
        min_x = std::min(c00.x, endpoint.x) - half_width;
        min_y = std::min(c00.y, endpoint.y) - half_width;
        max_x = std::max(c00.x, endpoint.x) + half_width;
        max_y = std::max(c00.y, endpoint.y) + half_width;
    }
    auto x0 = static_cast<std::int32_t>(std::ceil(min_x - 0.5f));
    auto y0 = static_cast<std::int32_t>(std::ceil(min_y - 0.5f));
    auto x1 = static_cast<std::int32_t>(std::ceil(max_x - 0.5f));
    auto y1 = static_cast<std::int32_t>(std::ceil(max_y - 0.5f));
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

    return backend.fill_solid_shape_surface(
        framebuffer.surface_handle(), x0, y0, x1, y1,
        shape_params, line_params, color).ok();
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
    (void)framebuffer; (void)depth; (void)dof; (void)lens; (void)clip;
    unsupported("apply_per_pixel_dof");
}

graph::RenderOpResult VulkanBackend::draw_node(Framebuffer& framebuffer, const RenderNode& node,
                                               const RenderState& state, const Camera& camera,
                                               int width, int height) {
    if (try_native_path_fill(*this, framebuffer, node, state)) return graph::RenderOpResult(graph::RenderOpOutcome{1});
    if (try_native_path_stroke(*this, framebuffer, node, state)) return graph::RenderOpResult(graph::RenderOpOutcome{1});
    if (try_native_solid_rect(*this, framebuffer, node, state)) return graph::RenderOpResult(graph::RenderOpOutcome{1});
    (void)camera; (void)width; (void)height;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_node: no legacy-node fallback attached; node='" +
        std::string(node.name) + "' shape_type=" +
        std::to_string(static_cast<int>(node.shape.type()))});
}

void VulkanBackend::apply_effect_stack(
    Framebuffer& framebuffer, const EffectStack& stack,
    const effects::EffectExecutionContext& context) {
    (void)framebuffer; (void)stack; (void)context;
    unsupported("apply_effect_stack");
}

void VulkanBackend::composite_layer(Framebuffer& destination, const Framebuffer& source,
                                    BlendMode mode, const std::optional<raster::BBox>& clip,
                                    CompositeOperator op) {
    // Fail closed: a GPU backend never blends pixels on the CPU. CPU-origin
    // assets must be materialized into native surfaces (ensure_native_surface
    // / upload_surface) before reaching this authority; a missing handle is a
    // contract violation by the caller, never a silent CPU fallback.
    if (destination.surface_handle() == runtime::kInvalidRenderSurfaceHandle ||
        source.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        throw std::invalid_argument(
            "VulkanBackend::composite_layer: missing native surface handle "
            "(CPU blit fallback was demolished; materialize surfaces first)");
    }
    if (mode != BlendMode::Normal || op != CompositeOperator::SourceOver) {
        throw std::runtime_error("VulkanBackend::composite_layer: only Normal/SourceOver is implemented");
    }
    if (destination.width() != source.width() || destination.height() != source.height()) {
        throw std::runtime_error("VulkanBackend::composite_layer: surface dimensions differ");
    }
#ifdef CHRONON3D_ENABLE_VULKAN
    composite_legacy_surface(destination, source, mode, clip);
#else
    unsupported("composite_layer");
#endif
}

graph::RenderOpResult VulkanBackend::draw_text_run(
    Framebuffer& framebuffer, const TextRunShape& shape,
    const glm::mat4& model_matrix, float opacity) {
    (void)framebuffer; (void)shape; (void)model_matrix; (void)opacity;
    return graph::RenderOpResult(graph::RenderBackendError{
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run: no legacy-node fallback attached"});
}

void VulkanBackend::apply_blur(
    Framebuffer& framebuffer, float radius,
    const std::optional<raster::BBox>& clip) {
    (void)framebuffer; (void)radius; (void)clip;
    unsupported("apply_blur");
}

} // namespace chronon3d::backends::vulkan
