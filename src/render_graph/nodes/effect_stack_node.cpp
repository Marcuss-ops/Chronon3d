// ============================================================================
// effect_stack_node.cpp — EffectStackNode implementation.
//
// Extracted from effect_stack_node.hpp so the public header doesn't need
// to #include <spdlog/spdlog.h>.
// ============================================================================

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

std::optional<raster::BBox> EffectStackNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> input_bboxes
) const {
    if (input_bboxes.empty() || !input_bboxes[0]) {
        return std::nullopt;
    }
    auto bbox = *input_bboxes[0];
    if (bbox.is_empty()) {
        // When the input bbox is empty (e.g. a degenerate shape whose
        // corners all project to w≈0, producing an inverted {max,max,min,min}
        // bbox that collapses to {0,0,0,0} after SourceNode clipping),
        // returning it as a valid BBox causes execute() to intersect it
        // with the clip rect → inverted local_clip → negative ROI
        // dimensions → acquire_temp_framebuffer crash.  Return nullopt
        // so execute() falls back to the full input framebuffer + the
        // executor-provided clip_rect, which is always well-formed.
        return std::nullopt;
    }
    const f32 spread = compute_max_effect_spread();
    if (spread <= 0.0f) {
        return bbox;
    }
    bbox.x0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.x0) - spread)));
    bbox.y0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.y0) - spread)));
    bbox.x1 = std::min(ctx.frame_input.width, static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + spread)));
    bbox.y1 = std::min(ctx.frame_input.height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + spread)));

    if (ctx.policy.diagnostics_enabled) {
        spdlog::info(
            "[EffectStackNode] input_bbox=({}, {})-({}, {}) spread={} output_bbox=({}, {})-({}, {})",
            input_bboxes[0]->x0,
            input_bboxes[0]->y0,
            input_bboxes[0]->x1,
            input_bboxes[0]->y1,
            spread,
            bbox.x0,
            bbox.y0,
            bbox.x1,
            bbox.y1
        );
    }

    // After spread expansion + clamping, the bbox could still be empty
    // (e.g. input was inverted and expansion didn't fix it).  Return
    // nullopt so execute() falls back to the full input framebuffer.
    if (bbox.is_empty()) {
        return std::nullopt;
    }
    return bbox;
}

NodeExecResult EffectStackNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes
) {
    if (inputs.empty() || !inputs[0]) {
        auto empty = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
        empty->clear(Color::transparent());
        return NodeExecResult{std::move(empty)};
    }

    const f32 spread = compute_max_effect_spread();
    auto pred_bbox = predicted_bbox(ctx, input_bboxes);

    OwnedFB result;
    if (spread > 0.0f && pred_bbox) {
        const raster::BBox out_bounds = *pred_bbox;
        const i32 out_w = std::max(1, out_bounds.x1 - out_bounds.x0);
        const i32 out_h = std::max(1, out_bounds.y1 - out_bounds.y0);

        result = ctx.acquire_owned_fb(out_w, out_h, true, out_bounds);

        const i32 intersect_x0 = std::max(inputs[0]->origin_x(), out_bounds.x0);
        const i32 intersect_y0 = std::max(inputs[0]->origin_y(), out_bounds.y0);
        const i32 intersect_x1 = std::min(inputs[0]->origin_x() + inputs[0]->width(), out_bounds.x1);
        const i32 intersect_y1 = std::min(inputs[0]->origin_y() + inputs[0]->height(), out_bounds.y1);

        if (intersect_x1 > intersect_x0 && intersect_y1 > intersect_y0) {
            const i32 w_copy = intersect_x1 - intersect_x0;
            for (i32 y = intersect_y0; y < intersect_y1; ++y) {
                const Color* src = inputs[0]->pixels_row(y - inputs[0]->origin_y()) + (intersect_x0 - inputs[0]->origin_x());
                Color* dst = result->pixels_row(y - out_bounds.y0) + (intersect_x0 - out_bounds.x0);
                std::copy_n(src, w_copy, dst);
            }
        }

        result->set_opaque(inputs[0]->is_opaque());
        result->set_key_digest(inputs[0]->key_digest());
    } else {
        result = ctx.acquire_owned_fb(*inputs[0]);
        // F3.2 (TICKET-GLOW-FULLFRAME-AUDIT-V1) — when spread==0, the
        // output framebuffer is the input framebuffer acquired full-frame.
        // The FramebufferPool's swap_contents placeholder pattern is
        // zero-copy (no byte memcpy) so we increment PASS-only here (every
        // pixel touched via metadata swap). The byte-side copy fallback
        // IS counted at the canonical std::copy site in
        // `framebuffer_acquire.cpp::acquire_framebuffer(const Framebuffer&)`
        // where the pool returns a re-used allocation (data ptr diff) and
        // std::copy runs. Splitting PASS from COPIES like this matches the
        // gate semantic: B03 CinematicGlow1080p must reach
        // `full_frame_copies_per_frame == 0` in steady state because the
        // glow spread drives bbox-dilated writes via the branch above (no
        // equivalent std::copy fallback fires).
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (ctx.services.backend) {
        std::optional<raster::BBox> local_clip = ctx.node_exec.clip_rect;
        if (pred_bbox) {
            if (local_clip) {
                local_clip->x0 = std::max(local_clip->x0, pred_bbox->x0);
                local_clip->y0 = std::max(local_clip->y0, pred_bbox->y0);
                local_clip->x1 = std::min(local_clip->x1, pred_bbox->x1);
                local_clip->y1 = std::min(local_clip->y1, pred_bbox->y1);
            } else {
                local_clip = pred_bbox;
            }
            // Guard: if the intersection produced an inverted clip
            // (x0 > x1 or y0 > y1), discard it so effect
            // implementations don't receive invalid region bounds.
            if (local_clip && local_clip->is_empty()) {
                local_clip = std::nullopt;
            }
        }
        // Effect implementations operate on the result framebuffer's local
        // pixel coordinates.  Spatial effects are commonly rendered into a
        // cropped ROI whose origin is expressed in canvas coordinates; pass
        // the clip translated into that ROI before dispatching the stack.
        if (local_clip) {
            local_clip->x0 -= result->origin_x();
            local_clip->y0 -= result->origin_y();
            local_clip->x1 -= result->origin_x();
            local_clip->y1 -= result->origin_y();
            local_clip->clip_to(result->width(), result->height());
            if (local_clip->is_empty()) {
                local_clip = std::nullopt;
            }
        }
        const effects::EffectExecutionContext effect_context{
            .time_seconds = ctx.frame_input.time_seconds,
            .frame = ctx.frame_input.frame,
            .clip = local_clip,
            .quality = effects::RenderQuality::Final,
            .diagnostics_enabled = ctx.policy.diagnostics_enabled
        };
        ctx.services.backend->apply_effect_stack(*result, m_effects, effect_context);
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
            uint64_t area = static_cast<uint64_t>(ctx.frame_input.width * ctx.frame_input.height);
            if (local_clip) {
                raster::BBox clipped = *local_clip;
                clipped.clip_to(ctx.frame_input.width, ctx.frame_input.height);
                if (!clipped.is_empty()) {
                    area = static_cast<uint64_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0);
                } else {
                    area = 0;
                }
            }
            ctx.node_exec.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
        }
    }
    return NodeExecResult{std::move(result)};
}

} // namespace chronon3d::graph
