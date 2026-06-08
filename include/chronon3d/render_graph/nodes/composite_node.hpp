#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class CompositeNode final : public RenderGraphNode {
public:
    bool cacheable() const override { return m_cache_frame >= 0; }

    CompositeNode(::chronon3d::BlendMode mode, Frame cache_frame = Frame{-1}, float world_z = 0.0f)
        : m_mode(mode), m_cache_frame(cache_frame), m_world_z(world_z) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Composite; }
    std::string name() const override { return "Composite"; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty()) return std::nullopt;
        if (input_bboxes.size() == 1) return input_bboxes[0];

        auto bottom = input_bboxes[0];
        auto top = input_bboxes[1];
        if (!bottom) return top;
        if (!top) return bottom;
        // Both inputs have valid (known) but empty bounding boxes.
        // Return a valid empty bbox (not nullopt) so the dirty-rect system
        // knows the output is empty rather than falling back to a full-frame render.
        if (bottom->is_empty() && top->is_empty()) {
            const i32 x = std::min(bottom->x0, top->x0);
            const i32 y = std::min(bottom->y0, top->y0);
            return raster::BBox{x, y, x, y};
        }

        raster::BBox union_box;
        union_box.x0 = std::min(bottom->x0, top->x0);
        union_box.y0 = std::min(bottom->y0, top->y0);
        union_box.x1 = std::max(bottom->x1, top->x1);
        union_box.y1 = std::max(bottom->y1, top->y1);
        return union_box;
    }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "composite",
            .frame = m_cache_frame >= 0 ? m_cache_frame : Frame{0},
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = static_cast<u64>(m_mode)
        };
    }

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override {
        if (inputs.size() < 2) {
            return inputs.empty() ? ctx.acquire_owned_fb(ctx.width, ctx.height) : ctx.acquire_owned_fb(*inputs[0]);
        }
        
        const FramebufferRef& bottom = inputs[0];
        const FramebufferRef& top = inputs[1];

        if (ctx.diagnostics_enabled) {
            spdlog::info(
                "[dirty-debug] frame={} Composite mode={} bottom_opaque={} top_opaque={} clip=[{}:{} -> {}:{}]",
                static_cast<int>(ctx.frame),
                static_cast<int>(m_mode),
                bottom ? (bottom->is_opaque() ? 1 : 0) : 0,
                top ? (top->is_opaque() ? 1 : 0) : 0,
                ctx.clip_rect ? ctx.clip_rect->x0 : 0,
                ctx.clip_rect ? ctx.clip_rect->y0 : 0,
                ctx.clip_rect ? ctx.clip_rect->x1 : ctx.width,
                ctx.clip_rect ? ctx.clip_rect->y1 : ctx.height
            );
        }

        // ── Skip-opaque optimization ─────────────────────────────────
        // When the top layer is fully opaque, covers the full frame,
        // and uses Normal blend mode, the bottom is completely hidden.
        // Swap pixel storage from the input framebuffer to avoid a
        // full-frame pixel copy.
        if (m_mode == BlendMode::Normal && top->is_opaque() &&
            input_bboxes.size() >= 2 && input_bboxes[1].has_value())
        {
            const auto& tb = *input_bboxes[1];
            if (tb.x0 <= 0 && tb.y0 <= 0 && tb.x1 >= ctx.width && tb.y1 >= ctx.height) {
                if (!ctx.clip_rect ||
                    (ctx.clip_rect->x0 <= 0 && ctx.clip_rect->y0 <= 0 &&
                     ctx.clip_rect->x1 >= ctx.width && ctx.clip_rect->y1 >= ctx.height))
                {
                    if (ctx.counters) {
                        ctx.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
                        const uint64_t area = static_cast<uint64_t>(ctx.width) * static_cast<uint64_t>(ctx.height);
                        ctx.counters->composite_copy_pixels.fetch_add(area, std::memory_order_relaxed);
                    }
                    auto result = ctx.acquire_owned_fb(top->width(), top->height(), false);
                    result->set_origin(top->origin_x(), top->origin_y());
                    result->set_opaque(true);
                    result->swap_contents(*top);
                    return result;
                }
            }
        }

        OwnedFB result;
        if (bottom->width() == ctx.width && bottom->height() == ctx.height) {
            result = ctx.acquire_owned_fb(*bottom);
        } else {
            result = ctx.acquire_owned_fb(ctx.width, ctx.height, true);
            if (ctx.backend) {
                ctx.backend->composite_layer(*result, *bottom, BlendMode::Normal);
            }
        }

        if (ctx.backend) {
            // Optimization: Only composite the area where the top node actually drew something
            std::optional<raster::BBox> clip = (input_bboxes.size() >= 2) ? input_bboxes[1] : std::nullopt;
            if (ctx.clip_rect) {
                if (clip) {
                    clip = raster::BBox{
                        .x0 = std::max(clip->x0, ctx.clip_rect->x0),
                        .y0 = std::max(clip->y0, ctx.clip_rect->y0),
                        .x1 = std::min(clip->x1, ctx.clip_rect->x1),
                        .y1 = std::min(clip->y1, ctx.clip_rect->y1)
                    };
                    if (clip->x0 >= clip->x1 || clip->y0 >= clip->y1) {
                        clip = raster::BBox{0, 0, 0, 0};
                    }
                } else {
                    clip = ctx.clip_rect;
                }
            }
            ctx.backend->composite_layer(*result, *top, m_mode, clip);

            // ── Per-pixel DOF depth tracking ──────────────────────────
            // When track_dof_depth is active, record the world_z of the
            // composited layer for each pixel where the source has alpha > 0.
            // Layers are composited back-to-front, so later (front) layers
            // overwrite the depth of earlier (back) layers — exactly what
            // we need for correct occlusion.
            if (ctx.track_dof_depth && !ctx.dof_depth.empty()) {
                const i32 w = ctx.width;
                const float wz = m_world_z;
                const i32 bx0 = clip ? clip->x0 : 0;
                const i32 by0 = clip ? clip->y0 : 0;
                const i32 bx1 = clip ? clip->x1 : ctx.width;
                const i32 by1 = clip ? clip->y1 : ctx.height;
                for (i32 y = by0; y < by1; ++y) {
                    const i32 sy = y - top->origin_y();
                    if (sy < 0 || sy >= top->height()) continue;
                    const Color* src_row = top->pixels_row(sy);
                    for (i32 x = bx0; x < bx1; ++x) {
                        const i32 sx = x - top->origin_x();
                        if (sx < 0 || sx >= top->width()) continue;
                        if (src_row[sx].a > 0.01f) {
                            ctx.dof_depth[static_cast<size_t>(y) * w + x] = wz;
                        }
                    }
                }
            }

            // Propagate opacity: when the top layer is opaque and covers the full
            // frame with Normal blend, the result inherits its opaque flag even
            // if the bottom was transparent (the top fully occludes it).
            if (m_mode == BlendMode::Normal && top->is_opaque() &&
                input_bboxes.size() >= 2 && input_bboxes[1].has_value() &&
                input_bboxes[1]->x0 <= 0 && input_bboxes[1]->y0 <= 0 &&
                input_bboxes[1]->x1 >= ctx.width && input_bboxes[1]->y1 >= ctx.height &&
                (!ctx.clip_rect ||
                 (ctx.clip_rect->x0 <= 0 && ctx.clip_rect->y0 <= 0 &&
                  ctx.clip_rect->x1 >= ctx.width && ctx.clip_rect->y1 >= ctx.height)))
            {
                result->set_opaque(true);
            }

            if (ctx.counters) {
                ctx.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
                uint64_t area = clip ? (static_cast<uint64_t>(std::max(0, clip->x1 - clip->x0)) * std::max(0, clip->y1 - clip->y0))
                                     : static_cast<uint64_t>(ctx.width * ctx.height);
                if (m_mode == BlendMode::Normal && top->is_opaque()) {
                    ctx.counters->composite_copy_pixels.fetch_add(area, std::memory_order_relaxed);
                } else {
                    ctx.counters->composite_pixels.fetch_add(area, std::memory_order_relaxed);
                }
            }
        }
        return result;
    }

    // ── Accessor for graph optimization ─────────────────────────────────
    [[nodiscard]] BlendMode blend_mode() const { return m_mode; }

private:
    BlendMode m_mode;
    Frame m_cache_frame{-1};
    float m_world_z{0.0f};
};

} // namespace chronon3d::graph
