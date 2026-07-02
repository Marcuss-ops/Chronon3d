// ============================================================================
// per_pixel_dof_node.cpp — PerPixelDofNode::execute() implementation.
//
// Moved out of the header to break the inline dependency on the software
// backend.  Uses DofKernelInterface from RenderResourceContext.
// ============================================================================

#include <chronon3d/render_graph/nodes/per_pixel_dof_node.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/core/profiling/counters.hpp>

namespace chronon3d::graph {

NodeExecResult PerPixelDofNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes)
{
    if (inputs.empty() || !inputs[0]) {
        auto empty = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
        empty->clear(Color::transparent());
        return NodeExecResult{std::move(empty)};
    }

    if (!m_camera.dof.enabled) {
        return ctx.acquire_owned_fb(*inputs[0]);
    }

    // Check that the depth buffer was populated during compositing
    if (ctx.node_exec.dof_depth.empty()) {
        // No depth data — fall through without blur
        return ctx.acquire_owned_fb(*inputs[0]);
    }

    auto result = ctx.acquire_owned_fb(*inputs[0]);

    // Determine clip region
    std::optional<raster::BBox> clip = ctx.node_exec.clip_rect;
    auto pred = predicted_bbox(ctx, input_bboxes);
    if (pred && clip) {
        clip->x0 = std::max(clip->x0, pred->x0);
        clip->y0 = std::max(clip->y0, pred->y0);
        clip->x1 = std::min(clip->x1, pred->x1);
        clip->y1 = std::min(clip->y1, pred->y1);
    }

    // Apply per-pixel DOF blur via the render backend.
    if (ctx.services.backend) {
        // Explicit span-wrap: `ctx.node_exec.dof_depth` is `std::vector<float>`
        // (the executor owns the depth buffer for node lifetimes).  The kernel
        // signature took `std::vector<float>` historically; that forced
        // `SoftwareBackend::apply_per_pixel_dof` to allocate a 1920×1080 copy
        // — 8 MiB — on every dispatch.  Forwarding a `std::span` makes that
        // alloc go away.  Implicit C++20 range-to-span conversion would also
        // work, but explicit construction documents intent and guards
        // against future type drift on `dof_depth`.
        ctx.services.backend->apply_per_pixel_dof(
            *result, std::span<const float>{ctx.node_exec.dof_depth},
            m_camera.dof, m_camera.lens, clip);
    }

    if (ctx.node_exec.counters) {
        ctx.node_exec.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
        uint64_t area = static_cast<uint64_t>(ctx.frame_input.width) * ctx.frame_input.height;
        if (clip) {
            raster::BBox clipped = *clip;
            clipped.clip_to(ctx.frame_input.width, ctx.frame_input.height);
            area = clipped.is_empty() ? 0
                : static_cast<uint64_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0);
        }
        ctx.node_exec.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
    }

    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
