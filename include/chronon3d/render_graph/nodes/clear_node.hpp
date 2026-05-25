#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class ClearNode final : public RenderGraphNode {
public:
    ClearNode() {
        set_frame_dependent(false);
    }

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Output; }
    std::string name() const override { return "Clear"; }

    bool cacheable() const override { return false; }

    std::optional<raster::BBox> predicted_bbox(const RenderGraphContext& ctx) const override {
        if (ctx.clip_rect) {
            return *ctx.clip_rect;
        }
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "clear",
            .frame = 0,
            .width = ctx.width,
            .height = ctx.height
        };
    }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        std::span<const std::shared_ptr<Framebuffer>>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(ctx.backend);
        bool use_dirty_rects = sw_renderer && ctx.reuse_prev_framebuffer && sw_renderer->m_prev_framebuffer;
        const bool skip_clear = ctx.skip_initial_clear && !use_dirty_rects;
        const uint64_t clear_pixels = ctx.clip_rect
            ? static_cast<uint64_t>(std::max(0, ctx.clip_rect->x1 - ctx.clip_rect->x0)) *
              static_cast<uint64_t>(std::max(0, ctx.clip_rect->y1 - ctx.clip_rect->y0))
            : static_cast<uint64_t>(ctx.width) * static_cast<uint64_t>(ctx.height);

        if (sw_renderer && ctx.diagnostics_enabled) {
            spdlog::info(
                "[dirty-debug] frame={} Clear reuse_prev={} clip=[{}:{} -> {}:{}] prev_origin=[{},{}] prev_opaque={}",
                static_cast<int>(ctx.frame),
                use_dirty_rects ? 1 : 0,
                ctx.clip_rect ? ctx.clip_rect->x0 : 0,
                ctx.clip_rect ? ctx.clip_rect->y0 : 0,
                ctx.clip_rect ? ctx.clip_rect->x1 : ctx.width,
                ctx.clip_rect ? ctx.clip_rect->y1 : ctx.height,
                sw_renderer->m_prev_framebuffer ? sw_renderer->m_prev_framebuffer->origin_x() : 0,
                sw_renderer->m_prev_framebuffer ? sw_renderer->m_prev_framebuffer->origin_y() : 0,
                sw_renderer->m_prev_framebuffer ? (sw_renderer->m_prev_framebuffer->is_opaque() ? 1 : 0) : 0
            );
        }
        
        if (use_dirty_rects) {
            auto fb = std::move(sw_renderer->m_prev_framebuffer);
            const bool is_empty_clip = ctx.clip_rect && ctx.clip_rect->is_empty();
            if (ctx.counters) {
                if (skip_clear || is_empty_clip) {
                    ctx.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                    ctx.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                }
                if (!is_empty_clip) {
                    ctx.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                    ctx.counters->clear_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                }
            }
            if (!is_empty_clip) {
                const auto t0 = std::chrono::high_resolution_clock::now();
                fb->clear(Color::transparent(), ctx.clip_rect);
                const auto t1 = std::chrono::high_resolution_clock::now();
                if (ctx.counters) {
                    const auto elapsed = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t1 - t0).count());
                    ctx.counters->clearnode_ms.fetch_add(elapsed, std::memory_order_relaxed);
                    ctx.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                }
            }
            return fb;
        } else {
            auto fb = ctx.acquire_framebuffer(ctx.width, ctx.height, !skip_clear, std::nullopt, ctx.counters ? &ctx.counters->clearnode_ms : nullptr);
            if (skip_clear) {
                if (ctx.counters) {
                    ctx.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                    ctx.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                }
                fb->set_opaque(false);
            }
            return fb;
        }
    }
};

} // namespace chronon3d::graph
