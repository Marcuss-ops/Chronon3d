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

    // CPU-exactness gate: this GPU path rasterises every segment as an
    // independent hard-edged SourceOver line, which is only equivalent to the
    // CPU stroker for a SINGLE straight OPAQUE segment:
    //   - flattening Quadratic/Cubic curves into 8/12 sub-segments
    //     double-blends the overlapping joints (visible seams when alpha<1)
    //     and leaves butt-cap notches on the outside of curved corners;
    //   - polyline joins carry no certified joint semantics shared with the
    //     CPU stroker;
    //   - translucent strokes accumulate alpha on every covered pixel.
    // Everything outside this subset is rejected by this backend as
    // UnsupportedCapability. This VulkanBackend has no implicit CPU stroker
    // fallback; the graph must surface the failure rather than change routes
    // silently. Slower-but-correct beats fast-but-wrong-pixels (backend
    // specialization must never change render semantics).
    // The pre-scan doubles as the point capture: a path that passes the gate
    // is exactly MoveTo + a single LineTo, so the points below are the only
    // ones the emission step can ever see. Keeping the capture here (instead
    // of a second command loop) guarantees no unreachable flattening/Close
    // emission branches can be re-introduced below the gate.
    int segments = 0;
    bool saw_move = false;
    Vec2 move_point{0.0f};
    Vec2 line_point{0.0f};
    for (const auto& command : path.commands) {
        switch (command.type) {
        case PathCommandType::MoveTo:
            if (saw_move) return false;  // multiple subpaths → unsupported
            saw_move = true;
            move_point = command.p0;
            break;
        case PathCommandType::LineTo:
            if (!saw_move || ++segments > 1) return false;  // polyline → CPU
            line_point = command.p0;
            break;
        case PathCommandType::Close:
        case PathCommandType::QuadraticTo:
        case PathCommandType::CubicTo:
            return false;  // curves / closed contours → exact CPU stroker
        }
    }
    if (!saw_move || segments != 1) return false;

    const float width = path.stroke.width;
    bool alpha_zero = false;
    const Color color = graph::native_promotion::premultiply(
        path.stroke.color, state.opacity, &alpha_zero);
    if (alpha_zero) return true;
    // Opaque-only: translucent strokes would double-blend (see gate comment).
    if (path.stroke.color.a < 1.0f || state.opacity < 1.0f) return false;

    const auto transform = [&state](Vec2 point) {
        const Vec4 transformed = state.matrix * Vec4(point, 0.0f, 1.0f);
        return Vec2{transformed.x, transformed.y};
    };
    bool emitted = false;
    bool ok = true;

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

    // Gate guarantees exactly one MoveTo + one LineTo; no Close/curve branch
    // can reach this point (the approximate flattening paths were demolished
    // with the pre-scan gate in 4f4a217d).
    emit(move_point, line_point);
    return emitted && ok;
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
        graph::native_promotion::has_active_mask(state) ||
        framebuffer.surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
        return false;
    }
    // Native primitives are deliberately restricted to fills and un-stroked
    // straight lines. Closed-shape strokes remain fail-closed until their
    // software/Vulkan pixel contract is validated; the Vulkan backend does not
    // have an implicit CPU fallback. Gradients and masks are also unsupported.
    const ShapeStroke* stroke_ptr = nullptr;
    if (type == ShapeType::Rect) stroke_ptr = &node.shape.rect().stroke;
    else if (type == ShapeType::RoundedRect) stroke_ptr = &node.shape.rounded_rect().stroke;
    else if (type == ShapeType::Circle) stroke_ptr = &node.shape.circle().stroke;
    const bool has_stroke = stroke_ptr && stroke_ptr->enabled && stroke_ptr->width > 0.0f;
    // The current SDF stroke shader has no CPU-pixel parity oracle yet. Keep
    // this capability fail-closed until alignment, fill/stroke precedence,
    // clipping, opacity, and edge coverage are certified against the software
    // rasterizer. VulkanBackend has no implicit CPU fallback.
    if (has_stroke) return false;

    Vec2 size{};
    Vec4 shape_params{0.0f};
    Vec4 line_params{0.0f};
    Color shape_color = node.color;
    const bool is_line = type == ShapeType::Line;
    if (type == ShapeType::Rect) {
        size = node.shape.rect().size;
        if (node.corner_radius > 0.0f) {
            shape_params = Vec4{1.0f, node.corner_radius, node.corner_radius, 0.0f};
        }
    } else if (type == ShapeType::RoundedRect) {
        size = node.shape.rounded_rect().size;
        shape_params = Vec4{1.0f, node.shape.rounded_rect().radius,
                            node.shape.rounded_rect().radius, 0.0f};
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
    if (!is_line && !graph::native_promotion::is_axis_aligned_affine(state)) {
        return false;
    }

    // Resolve the fill color exactly as the software shape processor does.
    bool fill_alpha_zero = false;
    const Color fill_premul = graph::native_promotion::premultiply(
        shape_color, state.opacity, &fill_alpha_zero);
    if (fill_alpha_zero) return true;

    // Geometry bounds in destination pixels (axis-aligned affine outer hull).
    const Vec4 c00 = state.matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const Vec4 c10 = state.matrix * Vec4(size.x, 0.0f, 0.0f, 1.0f);
    const Vec4 c01 = state.matrix * Vec4(0.0f, size.y, 0.0f, 1.0f);
    const Vec4 c11 = state.matrix * Vec4(size.x, size.y, 0.0f, 1.0f);
    float geom_min_x = std::min({c00.x, c10.x, c01.x, c11.x});
    float geom_min_y = std::min({c00.y, c10.y, c01.y, c11.y});
    float geom_max_x = std::max({c00.x, c10.x, c01.x, c11.x});
    float geom_max_y = std::max({c00.y, c10.y, c01.y, c11.y});

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

    return backend.fill_solid_shape_surface(
        framebuffer.surface_handle(), x0, y0, x1, y1,
        shape_params, line_params, fill_premul).ok();
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
    if (try_native_grid(*this, framebuffer, node, state)) return graph::RenderOpResult(graph::RenderOpOutcome{1});
    if (try_native_path_fill(*this, framebuffer, node, state)) return graph::RenderOpResult(graph::RenderOpOutcome{1});
    if (try_native_path_stroke(*this, framebuffer, node, state)) return graph::RenderOpResult(graph::RenderOpOutcome{1});
    if (try_native_solid_rect(*this, framebuffer, node, state)) return graph::RenderOpResult(graph::RenderOpOutcome{1});
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
    return graph::RenderOpResult(graph::make_render_backend_error(
        graph::RenderBackendErrorCode::UnsupportedCapability,
        "VulkanBackend::draw_text_run: no legacy-node fallback attached"));
}

void VulkanBackend::apply_blur(
    Framebuffer& framebuffer, float radius,
    const std::optional<raster::BBox>& clip) {
    (void)framebuffer; (void)radius; (void)clip;
    unsupported("apply_blur");
}

} // namespace chronon3d::backends::vulkan
