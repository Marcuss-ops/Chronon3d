// ============================================================================
// composite_node.cpp — CompositeNode::execute() implementation.
//
// Extracted from composite_node.hpp so the public header doesn't need
// to #include <spdlog/spdlog.h>.
// ============================================================================

#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::graph {

OwnedFB CompositeNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>> input_bboxes
) {
    if (inputs.size() < 2) {
        return inputs.empty() ? ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height) : ctx.acquire_owned_fb(*inputs[0]);
    }

    const FramebufferRef& bottom = inputs[0];
    const FramebufferRef& top = inputs[1];

    if (ctx.options.diagnostics_enabled) {
        spdlog::info(
            "[dirty-debug] frame={} Composite mode={} bottom_opaque={} top_opaque={} clip=[{}:{} -> {}:{}]",
            static_cast<int>(ctx.frame.frame),
            static_cast<int>(m_mode),
            bottom ? (bottom->is_opaque() ? 1 : 0) : 0,
            top ? (top->is_opaque() ? 1 : 0) : 0,
            ctx.tile.clip_rect ? ctx.tile.clip_rect->x0 : 0,
            ctx.tile.clip_rect ? ctx.tile.clip_rect->y0 : 0,
            ctx.tile.clip_rect ? ctx.tile.clip_rect->x1 : ctx.frame.width,
            ctx.tile.clip_rect ? ctx.tile.clip_rect->y1 : ctx.frame.height
        );
    }

    // Skip-opaque optimization — only applies to SourceOver + Normal blend
    if (m_operator == CompositeOperator::SourceOver &&
        m_mode == BlendMode::Normal && top->is_opaque() &&
        input_bboxes.size() >= 2 && input_bboxes[1].has_value())
    {
        const auto& tb = *input_bboxes[1];
        if (tb.x0 <= 0 && tb.y0 <= 0 && tb.x1 >= ctx.frame.width && tb.y1 >= ctx.frame.height) {
            if (!ctx.tile.clip_rect ||
                (ctx.tile.clip_rect->x0 <= 0 && ctx.tile.clip_rect->y0 <= 0 &&
                 ctx.tile.clip_rect->x1 >= ctx.frame.width && ctx.tile.clip_rect->y1 >= ctx.frame.height))
            {
                if (ctx.telemetry.counters) {
                    ctx.telemetry.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
                    const uint64_t area = static_cast<uint64_t>(ctx.frame.width) * static_cast<uint64_t>(ctx.frame.height);
                    ctx.telemetry.counters->composite_copy_pixels.fetch_add(area, std::memory_order_relaxed);
                }
                auto result = ctx.acquire_owned_fb(top->width(), top->height(), false);
                result->set_origin(top->origin_x(), top->origin_y());
                result->set_opaque(true);
                result->swap_contents(*top);
                return result;
            }
        }
    }

    // ── Acquire output framebuffer ────────────────────────────────────
    const auto t_acquire0 = profiling::now();
    OwnedFB result;
    if (bottom->width() == ctx.frame.width && bottom->height() == ctx.frame.height) {
        result = ctx.acquire_owned_fb(*bottom);
    } else {
        result = ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height, true);
        if (ctx.resources.backend) {
            ctx.resources.backend->composite_layer(*result, *bottom, BlendMode::Normal);
        }
    }
    const auto t_acquire1 = profiling::now();
    if (ctx.telemetry.counters) {
        const auto acquire_ms = static_cast<uint64_t>(
            profiling::duration_ms(t_acquire0, t_acquire1));
        ctx.telemetry.counters->compositenode_acquire_ms.fetch_add(acquire_ms, std::memory_order_relaxed);
    }
    // Note: when bottom doesn't fit (rare), the bottom composite_layer
    // call is included in this timing AND already tracked in
    // compositenode_blend_ms.  This minor double-counting is acceptable
    // because the common path (bottom fits, >=99% of frames) is pure.

    // Start dispatch timing here — AFTER acquire and optional bottom composite,
    // so dispatch_ms measures only clip/bbox computation + overhead, without
    // double-counting bottom composite time (already in compositenode_blend_ms).
    const auto t_dispatch0 = profiling::now();

    if (ctx.resources.backend) {
        std::optional<raster::BBox> clip = (input_bboxes.size() >= 2) ? input_bboxes[1] : std::nullopt;
        if (ctx.tile.clip_rect) {
            if (clip) {
                clip = raster::BBox{
                    .x0 = std::max(clip->x0, ctx.tile.clip_rect->x0),
                    .y0 = std::max(clip->y0, ctx.tile.clip_rect->y0),
                    .x1 = std::min(clip->x1, ctx.tile.clip_rect->x1),
                    .y1 = std::min(clip->y1, ctx.tile.clip_rect->y1)
                };
                if (clip->x0 >= clip->x1 || clip->y0 >= clip->y1) {
                    clip = raster::BBox{0, 0, 0, 0};
                }
            } else {
                clip = ctx.tile.clip_rect;
            }
        }

        // Declare these in the backend scope so the aggregate non-blend
        // calculation below can reference them regardless of whether the
        // inner telemetry blocks are entered.
        // Use auto (same as rest of codebase) — profiling::now() returns
        // a POD timestamp, default-constructed without clock syscall.
        auto _de = profiling::now(); // dispatch end
        auto _os = profiling::now(); // overhead start
        auto _oe = profiling::now(); // overhead end

        // Record dispatch time before composite_layer call.
        if (ctx.telemetry.counters) {
            _de = profiling::now();
            const auto dms = static_cast<uint64_t>(profiling::duration_ms(t_dispatch0, _de));
            ctx.telemetry.counters->compositenode_dispatch_ms.fetch_add(dms, std::memory_order_relaxed);
        }

        // Check if stencil/silhouette operator — these use the underlying
        // composite operator field instead of a blend mode when applicable.
        if (m_operator != CompositeOperator::SourceOver) {
            // Stencil/Silhouette: use Normal blend to copy top first, then
            // apply the operator via the backend (which handles the masking).
            // The operator is passed along so the backend can apply the
            // appropriate matte-style coverage to the backdrop.
            ctx.resources.backend->composite_layer(*result, *top, m_mode, clip, m_operator);
        } else {
            ctx.resources.backend->composite_layer(*result, *top, m_mode, clip);
        }

        // ── Post-blend overhead ────────────────────────────────────────
        _os = profiling::now();

        if (ctx.options.track_dof_depth && !ctx.telemetry.dof_depth.empty()) {
            const i32 w = ctx.frame.width;
            const float wz = m_world_z;
            const i32 bx0 = clip ? clip->x0 : 0;
            const i32 by0 = clip ? clip->y0 : 0;
            const i32 bx1 = clip ? clip->x1 : ctx.frame.width;
            const i32 by1 = clip ? clip->y1 : ctx.frame.height;
            for (i32 y = by0; y < by1; ++y) {
                const i32 sy = y - top->origin_y();
                if (sy < 0 || sy >= top->height()) continue;
                const Color* src_row = top->pixels_row(sy);
                for (i32 x = bx0; x < bx1; ++x) {
                    const i32 sx = x - top->origin_x();
                    if (sx < 0 || sx >= top->width()) continue;
                    if (src_row[sx].a > 0.01f) {
                        ctx.telemetry.dof_depth[static_cast<size_t>(y) * w + x] = wz;
                    }
                }
            }
        }

        if (m_mode == BlendMode::Normal && top->is_opaque() &&
            input_bboxes.size() >= 2 && input_bboxes[1].has_value() &&
            input_bboxes[1]->x0 <= 0 && input_bboxes[1]->y0 <= 0 &&
            input_bboxes[1]->x1 >= ctx.frame.width && input_bboxes[1]->y1 >= ctx.frame.height &&
            (!ctx.tile.clip_rect ||
             (ctx.tile.clip_rect->x0 <= 0 && ctx.tile.clip_rect->y0 <= 0 &&
              ctx.tile.clip_rect->x1 >= ctx.frame.width && ctx.tile.clip_rect->y1 >= ctx.frame.height)))
        {
            result->set_opaque(true);
        }

        if (ctx.telemetry.counters) {
            ctx.telemetry.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
            uint64_t area = clip ? (static_cast<uint64_t>(std::max(0, clip->x1 - clip->x0)) * std::max(0, clip->y1 - clip->y0))
                                 : static_cast<uint64_t>(ctx.frame.width * ctx.frame.height);
            if (m_mode == BlendMode::Normal && top->is_opaque()) {
                ctx.telemetry.counters->composite_copy_pixels.fetch_add(area, std::memory_order_relaxed);
            } else {
                ctx.telemetry.counters->composite_pixels.fetch_add(area, std::memory_order_relaxed);
            }
        }

        if (ctx.telemetry.counters) {
            _oe = profiling::now();
            const auto oms = static_cast<uint64_t>(
                profiling::duration_ms(_os, _oe));
            ctx.telemetry.counters->compositenode_overhead_ms.fetch_add(oms, std::memory_order_relaxed);
        }

        // Aggregate non-blend cost: (acquire + dispatch) + overhead.
        // Blend (composite_layer) is excluded because it runs between _de and _os.
        if (ctx.telemetry.counters) {
            const auto nb_work = profiling::duration_us(t_acquire0, _de)
                               + profiling::duration_us(_os, _oe);
            const auto nb_us = static_cast<uint64_t>(std::max(0.0, nb_work));
            ctx.telemetry.counters->compositenode_internal_us.fetch_add(
                nb_us, std::memory_order_relaxed);
        }
    }
    return result;
}

} // namespace chronon3d::graph
