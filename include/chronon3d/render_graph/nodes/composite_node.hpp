#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class CompositeNode final : public RenderGraphNode {
public:
    bool cacheable() const override { return m_cache_frame >= 0; }

    CompositeNode(::chronon3d::BlendMode mode, Frame cache_frame = Frame{-1}) : m_mode(mode), m_cache_frame(cache_frame) {}

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

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        std::span<const std::shared_ptr<Framebuffer>> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override {
        if (inputs.size() < 2) return inputs.empty() ? ctx.acquire_framebuffer(ctx.width, ctx.height) : inputs[0];
        
        const auto& bottom = inputs[0];
        const auto& top = inputs[1];

        if (ctx.diagnostics_enabled) {
            spdlog::info(
                "[dirty-debug] frame={} Composite mode={} bottom_use_count={} bottom_opaque={} top_opaque={} clip=[{}:{} -> {}:{}]",
                static_cast<int>(ctx.frame),
                static_cast<int>(m_mode),
                bottom.use_count(),
                bottom ? (bottom->is_opaque() ? 1 : 0) : 0,
                top ? (top->is_opaque() ? 1 : 0) : 0,
                ctx.clip_rect ? ctx.clip_rect->x0 : 0,
                ctx.clip_rect ? ctx.clip_rect->y0 : 0,
                ctx.clip_rect ? ctx.clip_rect->x1 : ctx.width,
                ctx.clip_rect ? ctx.clip_rect->y1 : ctx.height
            );
        }
        
        std::shared_ptr<Framebuffer> result;
        // Optimization: In-place composition if we are the unique owner of the bottom buffer
        if (ctx.optimize_compositing && bottom.use_count() == 1 && bottom->width() == ctx.width && bottom->height() == ctx.height) {
            result = bottom;
        } else {
            result = ctx.acquire_framebuffer(*bottom);
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
            if (ctx.counters) {
                ctx.counters->composite_calls.fetch_add(1, std::memory_order_relaxed);
                uint64_t area = clip ? (static_cast<uint64_t>(std::max(0, clip->x1 - clip->x0)) * std::max(0, clip->y1 - clip->y0))
                                     : static_cast<uint64_t>(ctx.width * ctx.height);
                if (m_mode == BlendMode::Normal && top->is_opaque()) {
                    ctx.counters->clear_copy_pixels.fetch_add(area, std::memory_order_relaxed);
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
};

} // namespace chronon3d::graph
