#include "composite_backdrop_detail.hpp"

#include "native_surface.hpp"

#include <algorithm>
#include <cmath>

namespace chronon3d::graph::detail {

std::optional<NodeExecutionError> apply_native_layer_dof(
    RenderGraphContext& ctx, const Framebuffer& layer, float camera_depth,
    float radius, float highlight_gain,
    const std::optional<raster::BBox>& layer_bounds, OwnedFB& output) {
    if (!ctx.backend || !ctx.surface_registry ||
        !ctx.backend_stats_api()->supports_native_surfaces() || radius < 0.5f)
        return std::nullopt;

    // Expand the cropped layer to a transparent canvas before blurring. This
    // gives the kernel room to spread alpha beyond the source's tight bounds.
    const int width = ctx.frame_input.width;
    const int height = ctx.frame_input.height;
    auto expanded = ctx.acquire_owned_fb(width, height, true);
    output = ctx.acquire_owned_fb(width, height, true);
    auto depth = ctx.acquire_owned_fb(width, height, true);
    auto scratch = ctx.acquire_owned_fb(width, height, true);
    // These three canvases are node-local intermediates. The graph owns only
    // `output`; without this guard every DoF node leaked three full-canvas
    // RGBA32F surfaces per frame and exhausted VRAM after ~128 frames.
    struct NativeScratchRelease {
        RenderGraphContext& ctx;
        Framebuffer* expanded;
        Framebuffer* depth;
        Framebuffer* scratch;
        ~NativeScratchRelease() {
            if (scratch) release_native_surface(ctx, *scratch);
            if (depth) release_native_surface(ctx, *depth);
            if (expanded) release_native_surface(ctx, *expanded);
        }
    } release{ctx, expanded.get(), depth.get(), scratch.get()};
    if (!ensure_native_surface(ctx, const_cast<Framebuffer&>(layer), "LayerDoF.source") ||
        !ensure_empty_native_surface(ctx, *expanded) ||
        !ensure_empty_native_surface(ctx, *output) ||
        !ensure_empty_native_surface(ctx, *depth))
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "LayerDoF", "failed to materialize Vulkan layer DOF surfaces"};

    auto* api = ctx.backend_surface_api();
    auto placed = api->transform_surface(expanded->surface_handle(),
        layer.surface_handle(), layer.origin_x(), layer.origin_y(), 1.0f);
    if (!placed.ok())
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "LayerDoF", "failed to place cropped layer on the expanded canvas"};
    expanded->mark_gpu_authoritative();
    auto copied = api->copy_surface_auxiliary(output->surface_handle(),
                                              expanded->surface_handle());
    if (!copied.ok())
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "LayerDoF", "failed to seed per-layer bokeh destination"};

    int x0 = 0, y0 = 0, x1 = width, y1 = height;
    if (layer_bounds) {
        const int pad = static_cast<int>(std::ceil(radius));
        x0 = std::clamp(layer_bounds->x0 - pad, 0, width);
        y0 = std::clamp(layer_bounds->y0 - pad, 0, height);
        x1 = std::clamp(layer_bounds->x1 + pad, 0, width);
        y1 = std::clamp(layer_bounds->y1 + pad, 0, height);
    }
    auto stamped = api->dof_depth_stamp_surface(depth->surface_handle(),
        expanded->surface_handle(), camera_depth, radius, x0, y0, x1, y1,
        0, 0, true, true);
    if (!stamped.ok())
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "LayerDoF", "failed to stamp per-layer depth and alpha coverage"};

    if (!ensure_empty_native_surface(ctx, *scratch))
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "LayerDoF", "failed to materialize scratch surface"};

    auto copied_scratch = api->copy_surface_auxiliary(scratch->surface_handle(),
                                                      expanded->surface_handle());
    if (!copied_scratch.ok())
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "LayerDoF", "failed to seed scratch surface"};

    auto h_blurred = api->dof_blur_surface(scratch->surface_handle(),
        expanded->surface_handle(), depth->surface_handle(), false, x0, y0, x1, y1,
        radius, false);
    if (!h_blurred.ok())
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "LayerDoF", "Vulkan horizontal layer DOF pass failed"};

    auto v_blurred = api->dof_blur_surface(output->surface_handle(),
        scratch->surface_handle(), depth->surface_handle(), true, x0, y0, x1, y1,
        radius, false);
    if (!v_blurred.ok())
        return NodeExecutionError{RenderBackendErrorCode::ExecutionFailure,
            "LayerDoF", "Vulkan vertical layer DOF pass failed"};

    output->mark_gpu_authoritative();
    return std::nullopt;
}

} // namespace chronon3d::graph::detail
