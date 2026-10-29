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

    const auto& light_context = ctx.frame_input.light_context;
    const bool has_layer_lights = light_context.has_point_lights();
    const bool per_pixel_lights = light_context.has_spatial_lights();
    if ((!light_context.enabled && !has_layer_lights) || !m_material.accepts_lights) {
        return ctx.acquire_owned_fb(*inputs[0]);
    }
    if (per_pixel_lights && !inputs[0]->data() && !ctx.policy.require_native_gpu) {
        return NodeExecResult(NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            std::string(name()),
            "per-pixel point lights require CPU pixel backing"});
    }

    const Vec3 normal_world = rendering::transform_normal(
        m_world_matrix, {0.0f, 0.0f, 1.0f});
    // evaluate_lighting() is constant for a flat 2.5D layer. Evaluate the
    // exact CPU contract once for white, then apply that multiplier in a
    // Vulkan compute pass to every non-transparent pixel. This preserves the
    // CPU look without reading a GPU-only framebuffer back into RAM.
    const f32 layer_z = m_world_matrix[3][2];
    const Color multiplier = rendering::evaluate_lighting_at(
        Color{1.0f, 1.0f, 1.0f, 1.0f}, normal_world,
        Vec3{0.5f, 0.5f, layer_z}, light_context, m_material);
    if (std::getenv("CHRONON3D_PATH_DIAG")) {
        spdlog::info(
            "[lighting-eval] layer={} enabled={} ambient_enabled={} directional_enabled={} point_lights={} normal=({:.4f},{:.4f},{:.4f}) multiplier=({:.4f},{:.4f},{:.4f},{:.4f}) material_accepts={} diffuse={:.4f}",
            m_layer_name, light_context.enabled,
            light_context.ambient_enabled, light_context.directional_enabled,
            light_context.point_lights.size(), normal_world.x, normal_world.y,
            normal_world.z, multiplier.r, multiplier.g, multiplier.b, multiplier.a,
            m_material.accepts_lights, m_material.diffuse);
    }

    // A constant multiplier is only exact for a flat scene light. Per-pixel
    // point lights vary across the layer, so they force the CPU path.
    const bool native_input =
        !per_pixel_lights && ctx.policy.require_native_gpu && ctx.backend && ctx.surface_registry &&
        inputs[0]->surface_handle() != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend_surface_api()->is_native_surface_valid(inputs[0]->surface_handle());
    if (native_input) {
        auto result = ctx.acquire_owned_fb(
            inputs[0]->width(), inputs[0]->height(), false,
            input_bboxes.empty() ? std::nullopt : input_bboxes[0]);
        result->set_origin(inputs[0]->origin_x(), inputs[0]->origin_y());
        const bool path_diag = std::getenv("CHRONON3D_PATH_DIAG") != nullptr;
        if (path_diag) {
            spdlog::info(
                "[lighting-surface] layer={} phase=before-clear result_ptr={} fb={}x{} handle={} encode_handle={} slot_ptr={} input={}x{}",
                m_layer_name, static_cast<const void*>(result.get()),
                result->width(), result->height(), result->surface_handle(),
                ctx.policy.native_video_encode_surface,
                static_cast<const void*>(ctx.node_exec.planned_physical_slot),
                inputs[0]->width(), inputs[0]->height());
        }
        if (!ensure_empty_native_surface(ctx, *result)) {
            return NodeExecResult(NodeExecutionError{
                RenderBackendErrorCode::ExecutionFailure,
                std::string(name()),
                "native lighting output promotion failed"});
        }
        if (path_diag) {
            const auto output_record = ctx.surface_registry->lookup(result->surface_handle());
            spdlog::info(
                "[lighting-surface] layer={} phase=after-clear result_ptr={} fb={}x{} handle={} registry={}x{}",
                m_layer_name, static_cast<const void*>(result.get()),
                result->width(), result->height(), result->surface_handle(),
                output_record ? output_record->desc.width : 0,
                output_record ? output_record->desc.height : 0);
        }
        const auto applied = ctx.backend_surface_api()->lighting_surface(
            result->surface_handle(), inputs[0]->surface_handle(), multiplier);
        if (applied.ok()) {
            result->set_opaque(inputs[0]->is_opaque());
            result->set_content_cleared(inputs[0]->is_content_cleared());
            return NodeExecResult{std::move(result)};
        }
        const auto destination_record = ctx.surface_registry->lookup(result->surface_handle());
        const auto source_record = ctx.surface_registry->lookup(inputs[0]->surface_handle());
        release_native_surface(ctx, *result);
        return NodeExecResult(NodeExecutionError{
            applied.error().code,
            std::string(name()),
            applied.error().message + "; framebuffer destination=" +
                std::to_string(result->width()) + "x" + std::to_string(result->height()) +
                (destination_record
                    ? "; registry destination=" + std::to_string(destination_record->desc.width) +
                        "x" + std::to_string(destination_record->desc.height)
                    : "; registry destination=missing") +
                "; framebuffer source=" + std::to_string(inputs[0]->width()) +
                "x" + std::to_string(inputs[0]->height()) +
                (source_record
                    ? "; registry source=" + std::to_string(source_record->desc.width) +
                        "x" + std::to_string(source_record->desc.height)
                    : "; registry source=missing")});
    }

    if (ctx.policy.require_native_gpu && !inputs[0]->data()) {
        const auto surface = inputs[0]->surface_handle();
        const bool native_surface_valid =
            ctx.backend && ctx.surface_registry &&
            surface != runtime::kInvalidRenderSurfaceHandle &&
            ctx.backend_surface_api()->is_native_surface_valid(surface);
        return NodeExecResult(NodeExecutionError{
            RenderBackendErrorCode::ExecutionFailure,
            std::string(name()),
            "lighting layer '" + m_layer_name + "' requires a valid native surface or CPU pixel backing; " +
                "surface_handle=" + std::to_string(surface) +
                " native_surface_valid=" + (native_surface_valid ? "true" : "false") +
                " gpu_authoritative=" + (inputs[0]->is_gpu_authoritative() ? "true" : "false")});
    }

    auto result = ctx.acquire_owned_fb(*inputs[0]);
    // Framebuffer pixels are canvas pixels; the pixel centre sits at +0.5 and the
    // layer depth is the translation z of its world matrix.
    for (i32 y = 0; y < result->height(); ++y) {
        Color* row = result->pixels_row(y);
        const f32 pixel_y = static_cast<f32>(result->origin_y() + y) + 0.5f;
        for (i32 x = 0; x < result->width(); ++x) {
            if (row[x].a <= 0.0f) continue;
            if (per_pixel_lights) {
                const Vec3 pixel{static_cast<f32>(result->origin_x() + x) + 0.5f, pixel_y, layer_z};
                row[x] = rendering::evaluate_lighting_at(
                    row[x], normal_world, pixel, light_context, m_material);
            } else {
                row[x] = rendering::evaluate_lighting(
                    row[x], normal_world, light_context, m_material);
            }
        }
    }
    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
