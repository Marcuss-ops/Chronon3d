// ============================================================================
// clear_node.cpp — ClearNode::execute() implementation.
//
// Extracted from clear_node.hpp so the public header doesn't need
// to #include <spdlog/spdlog.h>.
// ============================================================================

#include <chronon3d/render_graph/nodes/clear_node.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <cstring>
#include <memory>

namespace chronon3d::graph {

OwnedFB ClearNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(ctx.resources.backend);
    bool use_dirty_rects = sw_renderer && ctx.options.reuse_prev_framebuffer && sw_renderer->buffer_ring().prev_framebuffer();
    const bool skip_clear = ctx.options.skip_initial_clear && !use_dirty_rects;
    const uint64_t clear_pixels = ctx.tile.clip_rect
        ? static_cast<uint64_t>(std::max(0, ctx.tile.clip_rect->x1 - ctx.tile.clip_rect->x0)) *
          static_cast<uint64_t>(std::max(0, ctx.tile.clip_rect->y1 - ctx.tile.clip_rect->y0))
        : static_cast<uint64_t>(ctx.frame.width) * static_cast<uint64_t>(ctx.frame.height);

    if (sw_renderer && ctx.options.diagnostics_enabled) {
        spdlog::info(
            "[dirty-debug] frame={} Clear reuse_prev={} clip=[{}:{} -> {}:{}] prev_origin=[{},{}] prev_opaque={}",
            static_cast<int>(ctx.frame.frame),
            use_dirty_rects ? 1 : 0,
            ctx.tile.clip_rect ? ctx.tile.clip_rect->x0 : 0,
            ctx.tile.clip_rect ? ctx.tile.clip_rect->y0 : 0,
            ctx.tile.clip_rect ? ctx.tile.clip_rect->x1 : ctx.frame.width,
            ctx.tile.clip_rect ? ctx.tile.clip_rect->y1 : ctx.frame.height,
            sw_renderer->buffer_ring().prev_framebuffer() ? sw_renderer->buffer_ring().prev_framebuffer()->origin_x() : 0,
            sw_renderer->buffer_ring().prev_framebuffer() ? sw_renderer->buffer_ring().prev_framebuffer()->origin_y() : 0,
            sw_renderer->buffer_ring().prev_framebuffer() ? (sw_renderer->buffer_ring().prev_framebuffer()->is_opaque() ? 1 : 0) : 0
        );
    }

    if (use_dirty_rects) {
        if (ctx.scratch.ping_write.fb) {
            Framebuffer* write_fb = ctx.scratch.ping_write.fb;
            const Framebuffer* read_fb = sw_renderer->buffer_ring().prev_framebuffer().get();
            const int h = ctx.frame.height;
            const int logical_w = ctx.frame.width;
            const int stride = write_fb->allocated_width();
            const auto& clip = ctx.tile.clip_rect;
            const bool is_empty = clip && clip->is_empty();

            if (!is_empty && clip) {
                const bool is_full_clip = (clip->x0 <= 0 && clip->y0 <= 0 &&
                                           clip->x1 >= logical_w && clip->y1 >= h);
                if (!is_full_clip) {
                    const int cw  = clip->x1 - clip->x0;
                    const int ch  = clip->y1 - clip->y0;

                    // If the clip covers > 50% of the frame, skip restore
                    // entirely — more than half of restored pixels would be
                    // immediately zeroed by the subsequent clip clear anyway.
                    // Just full clear and record restore = 0.
                    // This saves ~204ms for large dirty rects.
                    const float clip_fraction = (cw > 0 && ch > 0)
                        ? static_cast<float>(cw) * ch / (logical_w * h)
                        : 0.0f;
                    if (clip_fraction > 0.5f) {
                        const auto t_clear0 = profiling::now();
                        chronon3d::framebuffer_clear_parallel(*write_fb, Color::transparent(), std::nullopt);
                        const auto t_clear1 = profiling::now();
                        // restore_ms stays 0 (skipped)
                        if (ctx.telemetry.counters) {
                            const auto elapsed = static_cast<uint64_t>(
                                profiling::duration_ms(t_clear0, t_clear1));
                            ctx.telemetry.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                            ctx.telemetry.counters->clearnode_ms.fetch_add(elapsed, std::memory_order_relaxed);
                            ctx.telemetry.counters->clearnode_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                            ctx.telemetry.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                            // Full frame pixels (full clear, not just clip)
                            ctx.telemetry.counters->clear_pixels.fetch_add(
                                static_cast<uint64_t>(logical_w) * h, std::memory_order_relaxed);
                        }
                    } else {
                        // Combined restore+clear pass: copy from read_fb
                        // skipping the clip rect area (which is zeroed in the
                        // same loop).  Merging into a single parallel_for
                        // avoids a separate TBB scheduling pass + cache misses.
                        const auto t_restore0 = profiling::now();
                        const Color* src_data = read_fb->data();
                        Color* dst_data = write_fb->data();
                        const int src_stride = read_fb->allocated_width();
                        const int fh = ctx.frame.height;
                        const int cx0 = clip->x0;
                        const int cy0 = clip->y0;
                        const int cx1_r = clip->x1;
                        const int cy1 = clip->y1;
                        const int copy_stride = std::min(stride, src_stride);
                        const size_t row_bytes =
                            static_cast<size_t>(copy_stride) * sizeof(Color);
                        const size_t cw_size =
                            static_cast<size_t>(cw) * sizeof(Color);

                        parallel_for_tracked(
                            tbb::blocked_range<int>(0, fh, 64),
                            [&](const tbb::blocked_range<int>& range) {
                                for (int y = range.begin(); y < range.end(); ++y) {
                                    Color* dst = dst_data + static_cast<size_t>(y) * stride;
                                    const Color* src = src_data + static_cast<size_t>(y) * src_stride;
                                    if (y < cy0 || y >= cy1) {
                                        // Rows outside clip: copy entire row from prev frame
                                        std::memcpy(dst, src, row_bytes);
                                    } else {
                                        // Rows inside clip: restore left + right segments
                                        // and zero the clip area in one row pass.
                                        if (cx0 > 0)
                                            std::memcpy(dst, src,
                                                static_cast<size_t>(cx0) * sizeof(Color));
                                        if (cx1_r < copy_stride)
                                            std::memcpy(dst + cx1_r, src + cx1_r,
                                                static_cast<size_t>(copy_stride - cx1_r) * sizeof(Color));
                                        // Clear the clip rect portion
                                        std::memset(dst + cx0, 0, cw_size);
                                    }
                                }
                            },
                            ctx.telemetry.counters,
                            tbb::simple_partitioner{});

                        uint64_t restore_elapsed = 0;
                        if (ctx.telemetry.counters) {
                            const auto t_restore1 = profiling::now();
                            restore_elapsed = static_cast<uint64_t>(
                                profiling::duration_ms(t_restore0, t_restore1));
                            ctx.telemetry.counters->clearnode_restore_ms.fetch_add(restore_elapsed, std::memory_order_relaxed);
                            ctx.telemetry.counters->clearnode_ms.fetch_add(restore_elapsed, std::memory_order_relaxed);
                            ctx.telemetry.counters->clearnode_clear_ms.fetch_add(restore_elapsed, std::memory_order_relaxed);
                            ctx.telemetry.counters->framebuffer_clear_ms.fetch_add(restore_elapsed, std::memory_order_relaxed);
                            ctx.telemetry.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                            ctx.telemetry.counters->clear_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                        }
                    }


                } else {
                    // Full clip: clear everything (no restore needed).
                    const auto t_clear0 = profiling::now();
                    chronon3d::framebuffer_clear_parallel(*write_fb, Color::transparent(), std::nullopt);
                    const auto t_clear1 = profiling::now();
                    if (ctx.telemetry.counters) {
                        const auto elapsed = static_cast<uint64_t>(
                            profiling::duration_ms(t_clear0, t_clear1));
                        ctx.telemetry.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.telemetry.counters->clearnode_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.telemetry.counters->clearnode_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.telemetry.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                        ctx.telemetry.counters->clear_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                    }
                }
            }

            write_fb->set_origin(0, 0);

            // The two-step (full clear + clip restore) above already handled
            // the clear for non-empty clips (both partial and full).
            // Only handle null/empty clip cases here.
            if (!clip || is_empty) {
                if (!is_empty) {
                    const auto t0 = profiling::now();
                    chronon3d::framebuffer_clear_parallel(*write_fb, Color::transparent(), std::nullopt);
                    const auto t1 = profiling::now();
                    if (ctx.telemetry.counters) {
                        const auto elapsed = static_cast<uint64_t>(
                            profiling::duration_ms(t0, t1));
                        ctx.telemetry.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.telemetry.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                        ctx.telemetry.counters->clear_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                    }
                }
                if (ctx.telemetry.counters) {
                    ctx.telemetry.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                    ctx.telemetry.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                }
            }

            ctx.scratch.ping_write.fb = nullptr;
            PoolFbDeleter deleter;
            deleter.owned_by_renderer = true;
            return OwnedFB(write_fb, std::move(deleter));
        }

        const uint64_t prev_use_count = static_cast<uint64_t>(
            sw_renderer->buffer_ring().prev_framebuffer().use_count());
        const bool fb_is_shared = prev_use_count > 1;
        auto fb = std::move(sw_renderer->buffer_ring().prev_framebuffer());
        const bool is_empty_clip = ctx.tile.clip_rect && ctx.tile.clip_rect->is_empty();
        const bool is_full_clip = !ctx.tile.clip_rect ||
            (ctx.tile.clip_rect->x0 <= 0 && ctx.tile.clip_rect->y0 <= 0 &&
             ctx.tile.clip_rect->x1 >= ctx.frame.width && ctx.tile.clip_rect->y1 >= ctx.frame.height);

        const uint64_t fb_size_bytes = fb->size_bytes();
        if (ctx.telemetry.counters) {
            ctx.telemetry.counters->prev_fb_use_count_sum.fetch_add(prev_use_count, std::memory_order_relaxed);
            ctx.telemetry.counters->prev_fb_use_count_samples.fetch_add(1, std::memory_order_relaxed);
            {
                uint64_t prev_peak = ctx.telemetry.counters->prev_fb_use_count_peak.load(std::memory_order_relaxed);
                while (prev_use_count > prev_peak &&
                       !ctx.telemetry.counters->prev_fb_use_count_peak.compare_exchange_weak(
                           prev_peak, prev_use_count, std::memory_order_relaxed)) {}
            }
        }
        if (fb_is_shared && !is_full_clip) {
            if (ctx.telemetry.counters) {
                ctx.telemetry.counters->clearnode_detach_shared_count.fetch_add(1, std::memory_order_relaxed);
                ctx.telemetry.counters->clearnode_copy_pixels.fetch_add(
                    static_cast<uint64_t>(fb->pixel_count()), std::memory_order_relaxed);
                ctx.telemetry.counters->clearnode_memcpy_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.telemetry.counters->clearnode_memcpy_bytes.fetch_add(fb_size_bytes, std::memory_order_relaxed);
                if (!is_full_clip && ctx.tile.clip_rect &&
                    !(ctx.tile.clip_rect->x0 <= 0 && ctx.tile.clip_rect->x0 <= 0 &&
                      ctx.tile.clip_rect->x1 >= ctx.frame.width && ctx.tile.clip_rect->y1 >= ctx.frame.height)) {
                    ctx.telemetry.counters->clearnode_partial_clip_copy_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            const auto t_memcpy0 = profiling::now();
            auto owned = ctx.acquire_owned_fb(fb->width(), fb->height(), false);
            owned->set_origin(fb->origin_x(), fb->origin_y());
            const int copy_h = fb->height();
            const int copy_w = fb->allocated_width();
            const int64_t total_pixels = static_cast<int64_t>(copy_h) * copy_w;
            const bool use_parallel_copy = total_pixels >= (int64_t{1} << 18) && copy_h >= 8;
            if (use_parallel_copy) {
                if (ctx.telemetry.counters) {
                    ctx.telemetry.counters->framebuffer_copy_parallel_calls.fetch_add(1, std::memory_order_relaxed);
                }
                if (owned->allocated_width() == copy_w) {                                parallel_for_tracked(
                                    tbb::blocked_range<int>(0, copy_h, 64),
                                    [&](const tbb::blocked_range<int>& range) {
                                        std::memcpy(
                                            owned->data() + static_cast<size_t>(range.begin()) * copy_w,
                                            fb->data()   + static_cast<size_t>(range.begin()) * copy_w,
                                            static_cast<size_t>(range.size()) * copy_w * sizeof(Color));
                                    },
                                    ctx.telemetry.counters,
                                    tbb::simple_partitioner{}
                                );
                } else {                                parallel_for_tracked(
                                    tbb::blocked_range<int>(0, copy_h, 64),
                                    [&](const tbb::blocked_range<int>& range) {
                                        for (int y = range.begin(); y < range.end(); ++y) {
                                            std::memcpy(owned->pixels_row(y), fb->pixels_row(y),
                                                         static_cast<size_t>(copy_w) * sizeof(Color));
                                        }
                                    },
                                    ctx.telemetry.counters,
                                    tbb::simple_partitioner{}
                                );
                }
            } else {
                if (owned->allocated_width() == copy_w) {
                    std::memcpy(owned->data(), fb->data(),
                                 static_cast<size_t>(copy_h) * copy_w * sizeof(Color));
                } else {
                    for (int y = 0; y < copy_h; ++y) {
                        std::memcpy(owned->pixels_row(y), fb->pixels_row(y),
                                     static_cast<size_t>(copy_w) * sizeof(Color));
                    }
                }
            }
            Framebuffer* raw = owned.release();
            if (ctx.resources.framebuffer_pool) {
                fb = std::shared_ptr<Framebuffer>(raw,
                    PoolFbDeleter{ctx.resources.framebuffer_pool.get(), ctx.resources.framebuffer_pool->alive_token()});
            } else {
                fb = std::shared_ptr<Framebuffer>(raw);
            }
            if (ctx.telemetry.counters) {
                const auto t_memcpy1 = profiling::now();
                const auto elapsed = static_cast<uint64_t>(profiling::duration_ms(t_memcpy0, t_memcpy1));
                ctx.telemetry.counters->clearnode_memcpy_ms.fetch_add(elapsed, std::memory_order_relaxed);
            }
        } else {
            if (ctx.telemetry.counters && fb_size_bytes > 0) {
                ctx.telemetry.counters->clearnode_bytes_avoided.fetch_add(
                    static_cast<uint64_t>(fb_size_bytes), std::memory_order_relaxed);
                if (fb_is_shared && is_full_clip) {
                    ctx.telemetry.counters->clearnode_full_clip_skip_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        {
            std::optional<raster::BBox> local_clip = ctx.tile.clip_rect;
            if (local_clip) {
                local_clip->x0 -= fb->origin_x();
                local_clip->x1 -= fb->origin_x();
                local_clip->y0 -= fb->origin_y();
                local_clip->y1 -= fb->origin_y();
                local_clip->clip_to(fb->width(), fb->height());
            }
            if (ctx.telemetry.counters) {
                if (skip_clear || is_empty_clip) {
                    ctx.telemetry.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                    ctx.telemetry.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                }
                if (!is_empty_clip) {
                    ctx.telemetry.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                    ctx.telemetry.counters->clear_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                }
            }
            if (!is_empty_clip) {
                const auto t0 = profiling::now();
                fb->clear(Color::transparent(), local_clip);
                const auto t1 = profiling::now();
                if (ctx.telemetry.counters) {
                    const auto elapsed = static_cast<uint64_t>(profiling::duration_ms(t0, t1));
                    ctx.telemetry.counters->clearnode_ms.fetch_add(elapsed, std::memory_order_relaxed);
                    ctx.telemetry.counters->clearnode_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                    ctx.telemetry.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                }
            }
            return ctx.acquire_owned_fb(std::move(fb));
        }
    }
    {
        auto fb = ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height, !skip_clear);
        if (skip_clear) {
            if (ctx.telemetry.counters) {
                ctx.telemetry.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.telemetry.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
            }
            fb->set_opaque(false);
        }
        return fb;
    }
}

} // namespace chronon3d::graph
