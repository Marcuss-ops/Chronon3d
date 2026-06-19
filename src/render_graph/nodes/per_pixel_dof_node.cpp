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

OwnedFB PerPixelDofNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes)
{
    if (inputs.empty() || !inputs[0]) {
        auto empty = ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height);
        empty->clear(Color::transparent());
        return empty;
    }

    if (!m_camera.dof.enabled) {
        return ctx.acquire_owned_fb(*inputs[0]);
    }

    // Check that the depth buffer was populated during compositing
    if (ctx.telemetry.dof_depth.empty()) {
        // No depth data — fall through without blur
        return ctx.acquire_owned_fb(*inputs[0]);
    }

    auto result = ctx.acquire_owned_fb(*inputs[0]);

    // Determine clip region
    std::optional<raster::BBox> clip = ctx.tile.clip_rect;
    auto pred = predicted_bbox(ctx, input_bboxes);
    if (pred && clip) {
        clip->x0 = std::max(clip->x0, pred->x0);
        clip->y0 = std::max(clip->y0, pred->y0);
        clip->x1 = std::min(clip->x1, pred->x1);
        clip->y1 = std::min(clip->y1, pred->y1);
    }

    // Apply per-pixel DOF blur via the render backend.
    if (ctx.resources.backend) {
        ctx.resources.backend->apply_per_pixel_dof(
            *result, ctx.telemetry.dof_depth, m_camera.dof, m_camera.lens, clip);
    }

    if (ctx.telemetry.counters) {
        ctx.telemetry.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
        uint64_t area = static_cast<uint64_t>(ctx.frame.width) * ctx.frame.height;
        if (clip) {
            raster::BBox clipped = *clip;
            clipped.clip_to(ctx.frame.width, ctx.frame.height);
            area = clipped.is_empty() ? 0
                : static_cast<uint64_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0);
        }
        ctx.telemetry.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
    }

    return result;
}

} // namespace chronon3d::graph
