#include <chronon3d/render_graph/nodes/lighting_node.hpp>

#include "native_surface.hpp"

#include <spdlog/spdlog.h>

namespace chronon3d::graph {

NodeExecResult LightingNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes) {
    if (inputs.empty() || !inputs[0]) {
        return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
    }

    if (!ctx.frame_input.light_context.enabled || !m_material.accepts_lights) {
        return ctx.acquire_owned_fb(*inputs[0]);
    }

    const Vec3 normal_world = rendering::transform_normal(
        m_world_matrix, {0.0f, 0.0f, 1.0f});
    // evaluate_lighting() is constant for a flat 2.5D layer. Evaluate the
    // exact CPU contract once for white, then apply that multiplier in a
    // Vulkan compute pass to every non-transparent pixel. This preserves the
    // CPU look without reading a GPU-only framebuffer back into RAM.
    const Color multiplier = rendering::evaluate_lighting(
        Color{1.0f, 1.0f, 1.0f, 1.0f}, normal_world,
        ctx.frame_input.light_context, m_material);

    const bool native_input =
        ctx.policy.require_native_gpu && ctx.backend && ctx.surface_registry &&
        inputs[0]->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend->is_native_surface_valid(inputs[0]->surface_handle());
    if (native_input) {
        auto result = ctx.acquire_owned_fb(
            inputs[0]->width(), inputs[0]->height(), false,
            input_bboxes.empty() ? std::nullopt : input_bboxes[0]);
        result->set_origin(inputs[0]->origin_x(), inputs[0]->origin_y());
        if (!ensure_empty_native_surface(ctx, *result)) {
            return NodeExecResult(NodeExecutionError{
                RenderBackendErrorCode::ExecutionFailure,
                std::string(name()),
                "native lighting output promotion failed"});
        }
        const auto applied = ctx.backend->lighting_surface(
            result->surface_handle(), inputs[0]->surface_handle(), multiplier);
        if (applied.ok()) {
            result->set_opaque(inputs[0]->is_opaque());
            result->set_content_cleared(inputs[0]->is_content_cleared());
            return NodeExecResult{std::move(result)};
        }
        release_native_surface(ctx, *result);
        return NodeExecResult(NodeExecutionError{
            applied.error().code,
            std::string(name()),
            applied.error().message});
    }

    if (ctx.policy.require_native_gpu && !inputs[0]->data()) {
        return NodeExecResult(NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            std::string(name()),
            "lighting requires a valid native surface or CPU pixel backing"});
    }

    auto result = ctx.acquire_owned_fb(*inputs[0]);
    for (i32 y = 0; y < result->height(); ++y) {
        Color* row = result->pixels_row(y);
        for (i32 x = 0; x < result->width(); ++x) {
            if (row[x].a <= 0.0f) continue;
            row[x] = rendering::evaluate_lighting(
                row[x], normal_world, ctx.frame_input.light_context, m_material);
        }
    }
    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
