// ============================================================================
// effect_stack_node.cpp — EffectStackNode implementation.
//
// Extracted from effect_stack_node.hpp so the public header doesn't need
// to #include <spdlog/spdlog.h>.
// ============================================================================

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
        return bbox;
    }
    const f32 spread = compute_max_effect_spread();
    if (spread <= 0.0f) {
        return bbox;
    }
    bbox.x0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.x0) - spread)));
    bbox.y0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.y0) - spread)));
    bbox.x1 = std::min(ctx.frame.width, static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + spread)));
    bbox.y1 = std::min(ctx.frame.height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + spread)));

    if (ctx.options.diagnostics_enabled) {
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

    if (bbox.is_empty()) {
        return bbox;
    }
    return bbox;
}

OwnedFB EffectStackNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes
) {
    if (inputs.empty() || !inputs[0]) {
        auto empty = ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height);
        empty->clear(Color::transparent());
        return empty;
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
    }

    if (ctx.resources.backend) {
        std::optional<raster::BBox> local_clip = ctx.tile.clip_rect;
        if (pred_bbox) {
            if (local_clip) {
                local_clip->x0 = std::max(local_clip->x0, pred_bbox->x0);
                local_clip->y0 = std::max(local_clip->y0, pred_bbox->y0);
                local_clip->x1 = std::min(local_clip->x1, pred_bbox->x1);
                local_clip->y1 = std::min(local_clip->y1, pred_bbox->y1);
            } else {
                local_clip = pred_bbox;
            }
        }
        ctx.resources.backend->apply_effect_stack(*result, m_effects, ctx.frame.time_seconds, local_clip);
        if (ctx.telemetry.counters) {
            ctx.telemetry.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
            uint64_t area = static_cast<uint64_t>(ctx.frame.width * ctx.frame.height);
            if (local_clip) {
                raster::BBox clipped = *local_clip;
                clipped.clip_to(ctx.frame.width, ctx.frame.height);
                if (!clipped.is_empty()) {
                    area = static_cast<uint64_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0);
                } else {
                    area = 0;
                }
            }
            ctx.telemetry.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
        }
    }
    return result;
}

} // namespace chronon3d::graph
