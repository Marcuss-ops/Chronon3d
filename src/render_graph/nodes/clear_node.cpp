// ============================================================================
// clear_node.cpp — ClearNode::execute() implementation.
//
// Extracted from clear_node.hpp so the public header doesn't need
// to #include <spdlog/spdlog.h>.
// ============================================================================

#include <chronon3d/render_graph/nodes/clear_node.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/parallel_tracked.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <cstring>
#include <memory>

namespace chronon3d::graph {

NodeExecResult ClearNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef>,
    std::span<const std::optional<raster::BBox>>
) {
    auto* sw_backend = dynamic_cast<SoftwareBackend*>(ctx.services.backend);
    auto* sw_renderer = static_cast<SoftwareRenderer*>(ctx.services.sw_renderer_sidecar);
    bool use_dirty_rects = sw_backend && ctx.policy.reuse_prev_framebuffer && false;
    const bool skip_clear = ctx.policy.skip_initial_clear && !use_dirty_rects;
    const uint64_t clear_pixels = ctx.node_exec.clip_rect
        ? static_cast<uint64_t>(std::max(0, ctx.node_exec.clip_rect->x1 - ctx.node_exec.clip_rect->x0)) *
          static_cast<uint64_t>(std::max(0, ctx.node_exec.clip_rect->y1 - ctx.node_exec.clip_rect->y0))
        : static_cast<uint64_t>(ctx.frame_input.width) * static_cast<uint64_t>(ctx.frame_input.height);

    if (sw_backend && ctx.policy.diagnostics_enabled) {
        spdlog::info(
            "[dirty-debug] frame={} Clear reuse_prev={} clip=[{}:{} -> {}:{}] prev_origin=[{},{}] prev_opaque={}",
            static_cast<int>(ctx.frame_input.frame),
            use_dirty_rects ? 1 : 0,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->x0 : 0,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->y0 : 0,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->x1 : ctx.frame_input.width,
            ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->y1 : ctx.frame_input.height,
            sw_renderer->buffer_ring().prev_framebuffer() ? sw_renderer->buffer_ring().prev_framebuffer()->origin_x() : 0,
            sw_renderer->buffer_ring().prev_framebuffer() ? sw_renderer->buffer_ring().prev_framebuffer()->origin_y() : 0,
            sw_renderer->buffer_ring().prev_framebuffer() ? (sw_renderer->buffer_ring().prev_framebuffer()->is_opaque() ? 1 : 0) : 0
        );
    }

    if (use_dirty_rects) {
        if (ctx.node_exec.ping_write.fb) {
            Framebuffer* write_fb = ctx.node_exec.ping_write.fb;
            const Framebuffer* read_fb = sw_renderer->buffer_ring().prev_framebuffer().get();
            const int h = ctx.frame_input.height;
            const int logical_w = ctx.frame_input.width;
            const int stride = write_fb->allocated_width();
            const auto& clip = ctx.node_exec.clip_rect;
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
                        // F3.2 (TICKET-GLOW-FULLFRAME-AUDIT-V1) — full-frame
                        // redundant clear (clip > 50% of frame, full clear+zero
                        // is cheaper than per-row clears). Counted as 1
                        // full_frame_pass.
                        if (ctx.node_exec.counters) {
                            ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
                        }
                        const auto t_clear1 = profiling::now();
                        if (ctx.node_exec.counters) {
                            const auto elapsed = static_cast<uint64_t>(
                                profiling::duration_ms(t_clear0, t_clear1));
                            ctx.node_exec.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                            ctx.node_exec.counters->clearnode_ms.fetch_add(elapsed, std::memory_order_relaxed);
                            ctx.node_exec.counters->clearnode_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                            ctx.node_exec.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                            ctx.node_exec.counters->clear_pixels.fetch_add(
                                static_cast<uint64_t>(logical_w) * h, std::memory_order_relaxed);
                            ctx.node_exec.counters->clearnode_restore_noop_count.fetch_add(1, std::memory_order_relaxed);
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
                        const int fh = ctx.frame_input.height;
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
                            ctx.node_exec.counters,
                            tbb::simple_partitioner{});

                        uint64_t restore_elapsed = 0;
                        if (ctx.node_exec.counters) {
                            const auto t_restore1 = profiling::now();
                            restore_elapsed = static_cast<uint64_t>(
                                profiling::duration_ms(t_restore0, t_restore1));
                            ctx.node_exec.counters->clearnode_restore_ms.fetch_add(restore_elapsed, std::memory_order_relaxed);
                            ctx.node_exec.counters->clearnode_ms.fetch_add(restore_elapsed, std::memory_order_relaxed);
                            ctx.node_exec.counters->clearnode_clear_ms.fetch_add(restore_elapsed, std::memory_order_relaxed);
                            ctx.node_exec.counters->framebuffer_clear_ms.fetch_add(restore_elapsed, std::memory_order_relaxed);
                            ctx.node_exec.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                            ctx.node_exec.counters->clear_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                            // ── Restore telemetry (dirty-region restore) ──
                            ctx.node_exec.counters->clearnode_restore_dirty_rect_count.fetch_add(1, std::memory_order_relaxed);
                            ctx.node_exec.counters->clearnode_restore_rect_count.fetch_add(1, std::memory_order_relaxed);
                            // Pixels restored = total frame minus clip area
                            const uint64_t frame_total = static_cast<uint64_t>(logical_w) * h;
                            const uint64_t restore_px = (frame_total > clear_pixels) ? (frame_total - clear_pixels) : 0;
                            ctx.node_exec.counters->clearnode_restore_pixels.fetch_add(restore_px, std::memory_order_relaxed);
                            ctx.node_exec.counters->clearnode_restore_bytes.fetch_add(restore_px * sizeof(Color), std::memory_order_relaxed);
                        }
                    }


                } else {
                    // Full clip: clear everything (no restore needed).
                    const auto t_clear0 = profiling::now();
                    chronon3d::framebuffer_clear_parallel(*write_fb, Color::transparent(), std::nullopt);
                    // F3.2 — full-frame redundant clear (clip covers full
                    // frame; the clear is the only work).
                    if (ctx.node_exec.counters) {
                        ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
                    }
                    const auto t_clear1 = profiling::now();
                    if (ctx.node_exec.counters) {
                        const auto elapsed = static_cast<uint64_t>(
                            profiling::duration_ms(t_clear0, t_clear1));
                        ctx.node_exec.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.node_exec.counters->clearnode_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.node_exec.counters->clearnode_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.node_exec.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                        ctx.node_exec.counters->clear_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                        ctx.node_exec.counters->clearnode_restore_noop_count.fetch_add(1, std::memory_order_relaxed);
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
                    // F3.2 — full-frame redundant clear (null clip → entire
                    // frame is cleared).
                    if (ctx.node_exec.counters) {
                        ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
                    }
                    const auto t1 = profiling::now();
                    if (ctx.node_exec.counters) {
                        const auto elapsed = static_cast<uint64_t>(
                            profiling::duration_ms(t0, t1));
                        ctx.node_exec.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.node_exec.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                        ctx.node_exec.counters->clear_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                    }
                }
                if (ctx.node_exec.counters) {
                    ctx.node_exec.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                    ctx.node_exec.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                    ctx.node_exec.counters->clearnode_restore_noop_count.fetch_add(1, std::memory_order_relaxed);
                }
            }

            ctx.node_exec.ping_write.fb = nullptr;
            PoolFbDeleter deleter;
            deleter.policy = RendererOwned{};
            return NodeExecResult{OwnedFB(write_fb, std::move(deleter))};
        }

        const uint64_t prev_use_count = static_cast<uint64_t>(
            sw_renderer->buffer_ring().prev_framebuffer().use_count());
        const bool fb_is_shared = prev_use_count > 1;
        auto fb = std::move(sw_renderer->buffer_ring().prev_framebuffer());
        const bool is_empty_clip = ctx.node_exec.clip_rect && ctx.node_exec.clip_rect->is_empty();
        const bool is_full_clip = !ctx.node_exec.clip_rect ||
            (ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
             ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width && ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height);

        const uint64_t fb_size_bytes = fb->size_bytes();
        if (ctx.node_exec.counters) {
            ctx.node_exec.counters->prev_fb_use_count_sum.fetch_add(prev_use_count, std::memory_order_relaxed);
            ctx.node_exec.counters->prev_fb_use_count_samples.fetch_add(1, std::memory_order_relaxed);
            {
                uint64_t prev_peak = ctx.node_exec.counters->prev_fb_use_count_peak.load(std::memory_order_relaxed);
                while (prev_use_count > prev_peak &&
                       !ctx.node_exec.counters->prev_fb_use_count_peak.compare_exchange_weak(
                           prev_peak, prev_use_count, std::memory_order_relaxed)) {}
            }
        }
        bool fresh_cleared = false;
        if (fb_is_shared && !is_full_clip) {
            // If the clip covers > 50% of the frame, skip the expensive
            // full-frame copy — most restored pixels would be immediately
            // zeroed by the clip clear anyway.  Just allocate a fresh FB
            // and full-clear it, saving both time and peak memory.
            const float clip_fraction = ctx.node_exec.clip_rect
                ? static_cast<float>(
                    (ctx.node_exec.clip_rect->x1 - ctx.node_exec.clip_rect->x0) *
                    (ctx.node_exec.clip_rect->y1 - ctx.node_exec.clip_rect->y0))
                  / (ctx.frame_input.width * ctx.frame_input.height)
                : 0.0f;
            if (clip_fraction > 0.5f) {
                if (ctx.node_exec.counters) {
                    ctx.node_exec.counters->clearnode_full_clip_skip_count.fetch_add(1, std::memory_order_relaxed);
                    ctx.node_exec.counters->clearnode_restore_noop_count.fetch_add(1, std::memory_order_relaxed);
                }
                auto owned = ctx.acquire_owned_fb(fb->width(), fb->height(), true);
                owned->set_origin(fb->origin_x(), fb->origin_y());
                // Fresh allocation is already zero-initialized; skip the
                // redundant clear in the shared block below.
                owned->set_content_cleared(true);
                fresh_cleared = true;
                Framebuffer* raw = owned.release();
                if (ctx.services.framebuffer_pool) {
                    fb = std::shared_ptr<Framebuffer>(raw,
                        PoolFbDeleter{ctx.services.framebuffer_pool});
                } else {
                    fb = std::shared_ptr<Framebuffer>(raw);
                }
            } else {
                // ── Dirty-region-aware restore for shared framebuffers ──
                // Instead of full memcpy, copy only pixels outside the clip
                // rect and zero the clip area in a single parallel_for pass.
                // This mirrors the ping_write path's combined restore+clear
                // and avoids copying pixels that will be immediately cleared.
                if (ctx.node_exec.counters) {
                    ctx.node_exec.counters->clearnode_detach_shared_count.fetch_add(1, std::memory_order_relaxed);
                    ctx.node_exec.counters->clearnode_memcpy_calls.fetch_add(1, std::memory_order_relaxed);
                }
                const auto t_restore0 = profiling::now();
                auto owned = ctx.acquire_owned_fb(fb->width(), fb->height(), false);
                owned->set_origin(fb->origin_x(), fb->origin_y());
                const Color* src_data = fb->data();
                Color* dst_data = owned->data();
                const int src_stride = fb->allocated_width();
                const int dst_stride = owned->allocated_width();
                const int fh = ctx.frame_input.height;
                const int fw = ctx.frame_input.width;
                const int copy_stride = std::min(dst_stride, src_stride);
                const size_t row_bytes = static_cast<size_t>(copy_stride) * sizeof(Color);
                // Use clip rect from context for dirty-region restore
                const int cx0 = ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->x0 : 0;
                const int cy0 = ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->y0 : 0;
                const int cx1 = ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->x1 : fw;
                const int cy1 = ctx.node_exec.clip_rect ? ctx.node_exec.clip_rect->y1 : fh;
                const int cw = cx1 - cx0;
                const size_t cw_bytes = static_cast<size_t>(std::max(0, cw)) * sizeof(Color);

                parallel_for_tracked(
                    tbb::blocked_range<int>(0, fh, 64),
                    [&](const tbb::blocked_range<int>& range) {
                        for (int y = range.begin(); y < range.end(); ++y) {
                            Color* dst = dst_data + static_cast<size_t>(y) * dst_stride;
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
                                if (cx1 < copy_stride)
                                    std::memcpy(dst + cx1, src + cx1,
                                        static_cast<size_t>(copy_stride - cx1) * sizeof(Color));
                                // Clear the clip rect portion
                                std::memset(dst + cx0, 0, cw_bytes);
                            }
                        }
                    },
                    ctx.node_exec.counters,
                    tbb::simple_partitioner{});

                // Mark as fresh_cleared so the subsequent clear below is skipped
                // (clip area was already zeroed in the loop above).
                owned->set_content_cleared(true);
                fresh_cleared = true;
                Framebuffer* raw = owned.release();
                if (ctx.services.framebuffer_pool) {
                    fb = std::shared_ptr<Framebuffer>(raw,
                        PoolFbDeleter{ctx.services.framebuffer_pool});
                } else {
                    fb = std::shared_ptr<Framebuffer>(raw);
                }
                if (ctx.node_exec.counters) {
                    const auto t_restore1 = profiling::now();
                    const auto elapsed = static_cast<uint64_t>(profiling::duration_ms(t_restore0, t_restore1));
                    ctx.node_exec.counters->clearnode_restore_ms.fetch_add(elapsed, std::memory_order_relaxed);
                    // New restore telemetry
                    ctx.node_exec.counters->clearnode_restore_dirty_rect_count.fetch_add(1, std::memory_order_relaxed);
                    ctx.node_exec.counters->clearnode_restore_rect_count.fetch_add(1, std::memory_order_relaxed);
                    const uint64_t frame_total = static_cast<uint64_t>(fw) * fh;
                    const uint64_t clip_total = static_cast<uint64_t>(std::max(0, cw)) * std::max(0, cy1 - cy0);
                    const uint64_t restore_px = (frame_total > clip_total) ? (frame_total - clip_total) : 0;
                    ctx.node_exec.counters->clearnode_restore_pixels.fetch_add(restore_px, std::memory_order_relaxed);
                    ctx.node_exec.counters->clearnode_restore_bytes.fetch_add(restore_px * sizeof(Color), std::memory_order_relaxed);
                }
            }
        } else {
            if (ctx.node_exec.counters) {
                if (fb_size_bytes > 0) {
                    ctx.node_exec.counters->clearnode_bytes_avoided.fetch_add(
                        static_cast<uint64_t>(fb_size_bytes), std::memory_order_relaxed);
                }
                if (fb_is_shared && is_full_clip) {
                    ctx.node_exec.counters->clearnode_full_clip_skip_count.fetch_add(1, std::memory_order_relaxed);
                }
                // Prev FB reused without copy: effectively a full-frame restore at zero cost.
                ctx.node_exec.counters->clearnode_restore_full_frame_count.fetch_add(1, std::memory_order_relaxed);
                ctx.node_exec.counters->clearnode_restore_noop_count.fetch_add(1, std::memory_order_relaxed);
                // F3.2 — the prev-FB reuse path touches every pixel
                // (full-frame restore). Surface in per-frame gate; the byte
                // win is captured by clearnode_bytes_avoided.
                ctx.node_exec.counters->full_frame_passes.fetch_add(1, std::memory_order_relaxed);
            }
        }
        {
            std::optional<raster::BBox> local_clip = ctx.node_exec.clip_rect;
            if (local_clip) {
                local_clip->x0 -= fb->origin_x();
                local_clip->x1 -= fb->origin_x();
                local_clip->y0 -= fb->origin_y();
                local_clip->y1 -= fb->origin_y();
                local_clip->clip_to(fb->width(), fb->height());
            }
            if (ctx.node_exec.counters) {
                if (skip_clear || is_empty_clip || fresh_cleared) {
                    ctx.node_exec.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                    ctx.node_exec.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                }
                if (!is_empty_clip && !fresh_cleared) {
                    ctx.node_exec.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                    ctx.node_exec.counters->clear_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                }
            }
            if (!is_empty_clip && !fresh_cleared) {
                const auto t0 = profiling::now();
                fb->clear(Color::transparent(), local_clip);
                const auto t1 = profiling::now();
                if (ctx.node_exec.counters) {
                    const auto elapsed = static_cast<uint64_t>(profiling::duration_ms(t0, t1));
                    ctx.node_exec.counters->clearnode_ms.fetch_add(elapsed, std::memory_order_relaxed);
                    ctx.node_exec.counters->clearnode_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                    ctx.node_exec.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                }
            }
            return NodeExecResult{ctx.acquire_owned_fb(std::move(fb))};
        }
    }
    {
        auto fb = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, !skip_clear);
        if (skip_clear) {
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->clear_skipped_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.node_exec.counters->clear_skipped_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
            }
            fb->set_opaque(false);
        }
        return NodeExecResult{std::move(fb)};
    }
}

} // namespace chronon3d::graph
