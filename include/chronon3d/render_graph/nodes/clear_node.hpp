#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chrono>
#include <cstring>
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

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override {
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

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef>,
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
            // Read use_count BEFORE std::move — after move it's always 1.
            const bool fb_is_shared = sw_renderer->m_prev_framebuffer.use_count() > 1;
            auto fb = std::move(sw_renderer->m_prev_framebuffer);
            const bool is_empty_clip = ctx.clip_rect && ctx.clip_rect->is_empty();
            // When the clip covers the full canvas, the clear will erase every pixel —
            // the previous frame content we'd copy via memcpy would be immediately
            // overwritten.  Skip the copy to save ~16ms/frame.
            const bool is_full_clip = !ctx.clip_rect ||
                (ctx.clip_rect->x0 <= 0 && ctx.clip_rect->y0 <= 0 &&
                 ctx.clip_rect->x1 >= ctx.width && ctx.clip_rect->y1 >= ctx.height);

            if (fb_is_shared && !is_full_clip) {
                // The previous framebuffer is also referenced by node-cache entries.
                // Clearing it in-place would corrupt cached sources, so detach first.
                // Use pool-acquired copy to avoid heap allocation overhead.
                const auto t_memcpy0 = std::chrono::high_resolution_clock::now();
                auto owned = ctx.acquire_owned_fb(fb->width(), fb->height(), false);
                owned->set_origin(fb->origin_x(), fb->origin_y());
                const int copy_h = fb->height();
                const int copy_w = fb->allocated_width();
                for (int y = 0; y < copy_h; ++y) {
                    std::memcpy(owned->pixels_row(y), fb->pixels_row(y),
                                 static_cast<size_t>(copy_w) * sizeof(Color));
                }
                Framebuffer* raw = owned.release();
                if (ctx.framebuffer_pool) {
                    fb = std::shared_ptr<Framebuffer>(raw,
                        PoolFbDeleter{ctx.framebuffer_pool.get(), ctx.framebuffer_pool->alive_token()});
                } else {
                    fb = std::shared_ptr<Framebuffer>(raw);
                }
                if (ctx.counters) {
                    const auto t_memcpy1 = std::chrono::high_resolution_clock::now();
                    const auto elapsed = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_memcpy1 - t_memcpy0).count());
                    ctx.counters->clearnode_memcpy_ms.fetch_add(elapsed, std::memory_order_relaxed);
                }
            }
            // ── Clear dirty area (shared path preserves previous FB content) ──
            std::optional<raster::BBox> local_clip = ctx.clip_rect;
            if (local_clip) {
                local_clip->x0 -= fb->origin_x();
                local_clip->x1 -= fb->origin_x();
                local_clip->y0 -= fb->origin_y();
                local_clip->y1 -= fb->origin_y();
                local_clip->clip_to(fb->width(), fb->height());
            }
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
                fb->clear(Color::transparent(), local_clip);
                const auto t1 = std::chrono::high_resolution_clock::now();
                if (ctx.counters) {
                    const auto elapsed = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t1 - t0).count());
                    ctx.counters->clearnode_ms.fetch_add(elapsed, std::memory_order_relaxed);
                    ctx.counters->clearnode_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                    ctx.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                }
            }
            return ctx.acquire_owned_fb(std::move(fb));
        }
        // Fresh FB path (no dirty rects, or previous FB is shared)
        {
            auto fb = ctx.acquire_owned_fb(ctx.width, ctx.height, !skip_clear);
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
