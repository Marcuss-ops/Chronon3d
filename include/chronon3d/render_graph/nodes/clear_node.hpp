#pragma once

#include <chronon3d/core/parallel_tracked.hpp>
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
        if (ctx.tile.clip_rect) {
            return *ctx.tile.clip_rect;
        }
        return raster::BBox{0, 0, ctx.frame.width, ctx.frame.height};
    }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "clear",
            .frame = 0,
            .width = ctx.frame.width,
            .height = ctx.frame.height
        };
    }

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(ctx.resources.backend);
        bool use_dirty_rects = sw_renderer && ctx.options.reuse_prev_framebuffer && sw_renderer->m_prev_framebuffer;
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
                sw_renderer->m_prev_framebuffer ? sw_renderer->m_prev_framebuffer->origin_x() : 0,
                sw_renderer->m_prev_framebuffer ? sw_renderer->m_prev_framebuffer->origin_y() : 0,
                sw_renderer->m_prev_framebuffer ? (sw_renderer->m_prev_framebuffer->is_opaque() ? 1 : 0) : 0
            );
        }
        
        if (use_dirty_rects) {
            // ── Ping-pong fast path: write ping is exclusive, no COW ──────────
            if (ctx.scratch.ping_write_fb) {
                Framebuffer* write_fb = ctx.scratch.ping_write_fb;
                const Framebuffer* read_fb = sw_renderer->m_prev_framebuffer.get();
                const int h = ctx.frame.height;
                const int logical_w = ctx.frame.width;
                const int stride = write_fb->allocated_width();
                const auto& clip = ctx.tile.clip_rect;
                const bool is_empty = clip && clip->is_empty();

                // ── Copy preserved content from read ping → write ping ──────
                // Uses a single contiguous memcpy instead of the old segmented
                // row-by-row approach.  The segmented copy issued ~2000 small
                // memcpy calls per frame (left+right segments × 1080 rows),
                // whose per-call overhead dominated (~315ms @ 0.15µs/call).
                // A single ~33MB memcpy at ERMSB speeds (~60 GB/s) costs ~0.5ms.
                // Dirty-rect pixels copied here are immediately overwritten by
                // the clear below — the wasting is negligible vs the loop win.
                // Track the time spent in this restore separately via
                // clearnode_restore_ms so ping-pong's new cost is visible.
                if (!is_empty && clip) {
                    // Full-clip: the clear below will overwrite every pixel —
                    // no point copying anything from the previous frame.
                    const bool is_full_clip = (clip->x0 <= 0 && clip->y0 <= 0 &&
                                               clip->x1 >= logical_w && clip->y1 >= h);
                    if (!is_full_clip) {
                        const auto t_restore0 = std::chrono::high_resolution_clock::now();
                        const Color* src_data = read_fb->data();
                        Color* dst_data = write_fb->data();
                        const int src_stride = read_fb->allocated_width();
                        const size_t row_bytes = static_cast<size_t>(logical_w) * sizeof(Color);

                        // Ping-pong buffers share the same pool bucket — strides
                        // are identical in practice.  When they match, a single
                        // contiguous memcpy handles the entire framebuffer.
                        if (stride == src_stride) {
                            std::memcpy(dst_data, src_data,
                                        static_cast<size_t>(h) * static_cast<size_t>(stride) * sizeof(Color));
                        } else {
                            // Defensive fallback: different strides → row-by-row.
                            // Full rows (not segmented) — large row copies still
                            // reach ERMSB speeds.
                            const int64_t total_pixels = static_cast<int64_t>(h) * logical_w;
                            if (total_pixels >= (int64_t{1} << 20) && h >= 16) {
                                parallel_for_tracked(
                                    tbb::blocked_range<int>(0, h, 16),
                                    [&](const tbb::blocked_range<int>& range) {
                                        for (int y = range.begin(); y < range.end(); ++y) {
                                            std::memcpy(dst_data + static_cast<size_t>(y) * stride,
                                                        src_data + static_cast<size_t>(y) * src_stride,
                                                        row_bytes);
                                        }
                                    },
                                    ctx.telemetry.counters);
                            } else {
                                for (int y = 0; y < h; ++y) {
                                    std::memcpy(dst_data + static_cast<size_t>(y) * stride,
                                                src_data + static_cast<size_t>(y) * src_stride,
                                                row_bytes);
                                }
                            }
                        }
                        if (ctx.telemetry.counters) {
                            const auto t_restore1 = std::chrono::high_resolution_clock::now();
                            const auto elapsed = static_cast<uint64_t>(
                                std::chrono::duration<double, std::milli>(t_restore1 - t_restore0).count());
                            ctx.telemetry.counters->clearnode_restore_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        }
                    }
                }

                // Reset origin (ping FB has origin 0,0 by construction).
                write_fb->set_origin(0, 0);

                // Clear dirty area (or entire FB when no clip = full canvas) and record timing.
                if (clip && !is_empty) {
                    const auto t0 = std::chrono::high_resolution_clock::now();
                    write_fb->clear(Color::transparent(), *clip);
                    const auto t1 = std::chrono::high_resolution_clock::now();
                    if (ctx.telemetry.counters) {
                        const auto elapsed = static_cast<uint64_t>(
                            std::chrono::duration<double, std::milli>(t1 - t0).count());
                        ctx.telemetry.counters->clearnode_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.telemetry.counters->clearnode_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.telemetry.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                        ctx.telemetry.counters->clear_calls.fetch_add(1, std::memory_order_relaxed);
                        ctx.telemetry.counters->clear_pixels.fetch_add(clear_pixels, std::memory_order_relaxed);
                    }
                } else {
                    // No clip (full canvas) or empty clip: clear entire FB.
                    if (!is_empty) {
                        const auto t0 = std::chrono::high_resolution_clock::now();
                        write_fb->clear(Color::transparent());
                        const auto t1 = std::chrono::high_resolution_clock::now();
                        if (ctx.telemetry.counters) {
                            const auto elapsed = static_cast<uint64_t>(
                                std::chrono::duration<double, std::milli>(t1 - t0).count());
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

                // Return write ping with owned_by_renderer deleter — the
                // renderer owns the ping buffer permanently via m_ping_fb[].
                // The deleter is a true no-op: it does NOT clear, delete, or
                // restore the FB.  This avoids the data race with the encoder
                // thread (which holds the CachedFB ref) and keeps the content
                // intact for the next frame's ClearNode read.
                ctx.scratch.ping_write_fb = nullptr;
                PoolFbDeleter deleter;
                deleter.owned_by_renderer = true;
                return OwnedFB(write_fb, std::move(deleter));
            }

            // ── Original COW path (fallback when ping-pong unavailable) ───────
            // Read use_count BEFORE std::move — after move it's always 1.
            const uint64_t prev_use_count = static_cast<uint64_t>(
                sw_renderer->m_prev_framebuffer.use_count());
            const bool fb_is_shared = prev_use_count > 1;
            auto fb = std::move(sw_renderer->m_prev_framebuffer);
            const bool is_empty_clip = ctx.tile.clip_rect && ctx.tile.clip_rect->is_empty();
            // When the clip covers the full canvas, the clear will erase every pixel —
            // the previous frame content we'd copy via memcpy would be immediately
            // overwritten.  Skip the copy to save ~16ms/frame.
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
                // The previous framebuffer is also referenced by the async
                // pipeline queue (writer thread still encoding it).
                // Clearing it in-place would corrupt the writer's data,
                // so detach first via a COW copy.
                //
                // Use pool-acquired copy to avoid heap allocation overhead.
                // A single contiguous memcpy at ERMSB speed (~1 cycle/16 bytes)
                // is ~3-4× faster than a segmented copy that skips the dirty
                // rect, because the loop overhead of ~1000 small memcpy calls
                // dominates (~0.5µs per call vs ~0.01µs for the extra bytes).
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
                const auto t_memcpy0 = std::chrono::high_resolution_clock::now();
                auto owned = ctx.acquire_owned_fb(fb->width(), fb->height(), false);
                owned->set_origin(fb->origin_x(), fb->origin_y());
                const int copy_h = fb->height();
                const int copy_w = fb->allocated_width();
                // Threshold for parallel copy: ~1M pixels (16MB @ 16 bytes/pixel).
                // Below that, a single memcpy is faster due to ERMSB microcode;
                // above it, splitting into row bands across cores wins.
                const int64_t total_pixels = static_cast<int64_t>(copy_h) * copy_w;
                const bool use_parallel_copy = total_pixels >= (int64_t{1} << 20) && copy_h >= 16;
                if (use_parallel_copy) {
                    if (ctx.telemetry.counters) {
                        ctx.telemetry.counters->framebuffer_copy_parallel_calls.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (owned->allocated_width() == copy_w) {
                        // Same stride: each band copies a contiguous block.
                        parallel_for_tracked(
                            tbb::blocked_range<int>(0, copy_h, 16),
                            [&](const tbb::blocked_range<int>& range) {
                                std::memcpy(
                                    owned->data() + static_cast<size_t>(range.begin()) * copy_w,
                                    fb->data()   + static_cast<size_t>(range.begin()) * copy_w,
                                    static_cast<size_t>(range.size()) * copy_w * sizeof(Color));
                            },
                            ctx.telemetry.counters
                        );
                    } else {
                        // Different stride: each band copies its own rows.
                        parallel_for_tracked(
                            tbb::blocked_range<int>(0, copy_h, 16),
                            [&](const tbb::blocked_range<int>& range) {
                                for (int y = range.begin(); y < range.end(); ++y) {
                                    std::memcpy(owned->pixels_row(y), fb->pixels_row(y),
                                                 static_cast<size_t>(copy_w) * sizeof(Color));
                                }
                            },
                            ctx.telemetry.counters
                        );
                    }
                } else {
                    // Sequential path for small framebuffers.
                    // When both FBs have the same stride, use a single contiguous
                    // memcpy instead of a row-by-row loop.  The CPU's ERMSB
                    // (Enhanced REP MOVSB) microcode handles large contiguous
                    // copies at ~1 cycle per 16 bytes — significantly faster than
                    // 1080 individual memcpy calls with per-call overhead.
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
                        PoolFbDeleter{ctx.framebuffer_pool.get(), ctx.resources.framebuffer_pool->alive_token()});
                } else {
                    fb = std::shared_ptr<Framebuffer>(raw);
                }
                if (ctx.telemetry.counters) {
                    const auto t_memcpy1 = std::chrono::high_resolution_clock::now();
                    const auto elapsed = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_memcpy1 - t_memcpy0).count());
                    ctx.telemetry.counters->clearnode_memcpy_ms.fetch_add(elapsed, std::memory_order_relaxed);
                }
            } else {
                // COW: framebuffer is not shared OR full-clip clear will
                // overwrite everything — no copy needed.  Track bytes avoided.
                if (ctx.telemetry.counters && fb_size_bytes > 0) {
                    ctx.telemetry.counters->clearnode_bytes_avoided.fetch_add(
                        static_cast<uint64_t>(fb_size_bytes), std::memory_order_relaxed);
                    // Track reason: was it skipped because the clip is full?
                    if (fb_is_shared && is_full_clip) {
                        ctx.telemetry.counters->clearnode_full_clip_skip_count.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
            // ── Clear dirty area (shared path preserves previous FB content) ──
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
                const auto t0 = std::chrono::high_resolution_clock::now();
                fb->clear(Color::transparent(), local_clip);
                const auto t1 = std::chrono::high_resolution_clock::now();
                if (ctx.telemetry.counters) {
                    const auto elapsed = static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t1 - t0).count());
                    ctx.telemetry.counters->clearnode_ms.fetch_add(elapsed, std::memory_order_relaxed);
                    ctx.telemetry.counters->clearnode_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                    ctx.telemetry.counters->framebuffer_clear_ms.fetch_add(elapsed, std::memory_order_relaxed);
                }
            }
            return ctx.acquire_owned_fb(std::move(fb));
        }
        // Fresh FB path (no dirty rects, or previous FB is shared)
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
};

} // namespace chronon3d::graph
