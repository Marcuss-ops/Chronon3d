#include "pipe_export_session.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <spdlog/spdlog.h>

#include <optional>
#include <thread>

namespace chronon3d::cli {

// ── Writer thread ───────────────────────────────────────────────────────────

void run_writer_thread(const WriterThreadContext& ctx) {
    profiling::g_current_counters = ctx.renderer.counters();
    profiling::g_current_framebuffer_pool = ctx.renderer.framebuffer_pool().get();
    bool arena_notified = false;

    for (;;) {
        RenderFramePackage package;
        const auto pop_t0 = profiling::now();
        bool was_idle = false;

        while (!ctx.queue.try_dequeue(package)) {
            if (ctx.writer_done.load()) {
                if (!ctx.queue.try_dequeue(package)) break;
            } else if (ctx.writer_failed.load()) {
                break;
            } else {
                was_idle = true;
                std::this_thread::yield();
                continue;
            }
        }

        if (ctx.writer_failed.load() ||
            (ctx.writer_done.load() && !package.framebuffer))
            break;

        const auto pop_t1 = profiling::now();
        const uint64_t dequeue_ms = static_cast<uint64_t>(
            profiling::duration_ms(pop_t0, pop_t1));

        if (ctx.renderer.counters()) {
            ctx.renderer.counters()->io_queue_pop_wait_ms.fetch_add(
                dequeue_ms, std::memory_order_relaxed);
            if (was_idle) {
                ctx.renderer.counters()->io_writer_idle_wait_ms.fetch_add(
                    dequeue_ms, std::memory_order_relaxed);
            }
        }

        if (package.framebuffer) {
            if (!arena_notified) {
                spdlog::info("[video] Exporting via Arena-backed SIMD pipeline");
                arena_notified = true;
            }

            const auto enc_t0 = profiling::now();
            // Capture the reference BEFORE moving the shared_ptr —
            // C++ argument evaluation order is unspecified.
            const Framebuffer& fb_ref = *package.framebuffer;
            if (!ctx.encoder.write_frame_async(fb_ref,
                                                std::move(package.framebuffer))) {
                ctx.writer_failed.store(true);
                return;
            }
            const auto enc_t1 = profiling::now();
            const uint64_t enc_us = static_cast<uint64_t>(
                profiling::duration_us(enc_t0, enc_t1));
            ctx.writer_encode_us_total.fetch_add(enc_us, std::memory_order_relaxed);

            ctx.frame_encoder_telemetry.push_back({
                .frame_number = package.frame_number,
                .conversion_copy_ms = ctx.encoder.last_frame_telemetry().conversion_copy_ms,
                .encoder_ms = ctx.encoder.last_frame_telemetry().encoder_ms,
                .pipe_write_ms = ctx.encoder.last_frame_telemetry().pipe_write_ms,
                .native_convert_ms = ctx.encoder.last_frame_telemetry().native_convert_ms,
                .native_send_ms = ctx.encoder.last_frame_telemetry().native_send_ms,
                .native_receive_ms = ctx.encoder.last_frame_telemetry().native_receive_ms,
                .native_mux_ms = ctx.encoder.last_frame_telemetry().native_mux_ms,
            });
        }

        ctx.triple_arena.release(package.arena);
    }
}

// ── Render loop ─────────────────────────────────────────────────────────────

RenderLoopResult run_render_loop(const RenderLoopContext& ctx) {
    RenderLoopResult result;
    auto& status = result.status;
    const int total = static_cast<int>(ctx.end - ctx.start);
    Frame current_frame = ctx.start;

    try {
        for (; current_frame < ctx.end; ++current_frame) {
            // Graceful cancellation (SIGINT/SIGTERM)
            if (ctx.opts.cancellation_token &&
                ctx.opts.cancellation_token->is_cancelled()) {
                mark_pipe_cancelled(status, current_frame);
                break;
            }

            if (ctx.writer_failed.load()) {
                mark_pipe_writer_failed(status, current_frame);
                break;
            }

            int done_count = static_cast<int>(current_frame - ctx.start + 1);
            if (should_log_pipe_progress(done_count, total)) {
                spdlog::info("[video]   {}/{} frames", done_count, total);
            }

            auto current_arena = ctx.triple_arena.acquire();
            if (ctx.sw_renderer) {
                ctx.sw_renderer->framebuffer_pool()->set_arena(current_arena);
            }

            const auto frame_t0 = profiling::now();
            auto fb = graph::render_composition_frame(
                ctx.backend, ctx.node_cache, ctx.settings, &ctx.registry,
                ctx.video_decoder, ctx.comp, current_frame);
            const auto frame_t1 = profiling::now();
            const double frame_ms =
                profiling::duration_ms(frame_t0, frame_t1);
            result.render_graph_eval_ms += frame_ms;

            if (ctx.counters) {
                ctx.counters->video_graph_eval_ms.fetch_add(
                    static_cast<uint64_t>(frame_ms), std::memory_order_relaxed);
            }

            const double dirty_ratio =
                ctx.sw_renderer ? ctx.sw_renderer->last_dirty_area_ratio() : 1.0;
            const bool dirty_rect_enabled =
                ctx.sw_renderer ? ctx.sw_renderer->last_dirty_rect_enabled() : false;
            const auto dirty_rect =
                ctx.sw_renderer ? ctx.sw_renderer->last_dirty_rect() : std::nullopt;
            const bool tile_execution_used =
                ctx.sw_renderer ? ctx.sw_renderer->last_tile_execution_used() : false;
            const bool fast_path_reused =
                ctx.sw_renderer ? ctx.sw_renderer->last_fast_path_reused() : false;
            const bool graph_reused =
                ctx.sw_renderer ? ctx.sw_renderer->last_graph_reused() : false;

            if (!fb) {
                ctx.triple_arena.release(current_arena);
                mark_pipe_render_failed(status, current_frame);
                break;
            }

            // ── Track active vs cached frame metrics ───────────────────
            // fast_path_reused=true means the graph was entirely skipped
            // (frame output reused from previous frame).  These are "free"
            // frames.  fast_path_reused=false means the graph executed.
            // We track both count and cumulative ms so the telemetry report
            // can compute avg_frame_ms_active vs avg_frame_ms_cached
            // without dilution from the large number of cached frames.
            if (ctx.counters) {
                if (fast_path_reused) {
                    ctx.counters->graph_skipped_frames.fetch_add(1, std::memory_order_relaxed);
                    ctx.counters->graph_skipped_ms_sum.fetch_add(
                        static_cast<uint64_t>(frame_ms * 1000.0), std::memory_order_relaxed);
                } else {
                    ctx.counters->graph_executed_frames.fetch_add(1, std::memory_order_relaxed);
                    ctx.counters->graph_executed_ms_sum.fetch_add(
                        static_cast<uint64_t>(frame_ms * 1000.0), std::memory_order_relaxed);
                }
            }

            // High-throughput enqueue
            const auto wait_t0 = profiling::now();
            const auto q_size = ctx.queue.size_approx();

            if (ctx.counters) {
                auto current_peak =
                    ctx.counters->io_queue_peak_depth.load(std::memory_order_relaxed);
                while (q_size > current_peak &&
                       !ctx.counters->io_queue_peak_depth.compare_exchange_weak(
                           current_peak, q_size, std::memory_order_relaxed)) {
                }
            }

            // Back-pressure: yield while the queue is saturated.
            while (ctx.queue.size_approx() > kMaxRenderQueueDepth) {
                if (ctx.writer_failed.load()) {
                    mark_pipe_writer_failed(status, current_frame);
                    break;
                }
                std::this_thread::yield();
            }

            if (status.cancelled || status.writer_error || status.render_failed) {
                ctx.triple_arena.release(current_arena);
                break;
            }

            ctx.queue.enqueue(
                RenderFramePackage{.frame_number = current_frame,
                                   .framebuffer = std::move(fb),
                                   .arena = std::move(current_arena)});

            const auto wait_t1 = profiling::now();
            const double wait_ms =
                profiling::duration_ms(wait_t0, wait_t1);
            result.queue_wait_ms += wait_ms;

            if (ctx.counters) {
                ctx.counters->io_queue_push_blocked_ms.fetch_add(
                    static_cast<uint64_t>(wait_ms), std::memory_order_relaxed);
            }

            ++status.frames_written;

            ctx.telemetry_frames.push_back({
                .frame_number = static_cast<int>(current_frame),
                .duration_ms = frame_ms + wait_ms,
                .cache_hit = true,
                .dirty_area_ratio = dirty_ratio,
                .graph_eval_ms = frame_ms,
                .queue_wait_ms = wait_ms,
                .dirty_rect_enabled = dirty_rect_enabled,
                .dirty_rect_x0 = dirty_rect ? dirty_rect->x0 : 0,
                .dirty_rect_y0 = dirty_rect ? dirty_rect->y0 : 0,
                .dirty_rect_x1 = dirty_rect ? dirty_rect->x1 : 0,
                .dirty_rect_y1 = dirty_rect ? dirty_rect->y1 : 0,
                .tile_execution_used = tile_execution_used,
                .fast_path_reused = fast_path_reused,
                .graph_reused = graph_reused
            });
        }
    } catch (const std::exception& e) {
        mark_pipe_exception(status, current_frame, e);
    }

    // All frames rendered without interruption → mark success.
    // The mark_* functions set success=false on failures/breaks.
    if (current_frame == ctx.end && !status.cancelled &&
        !status.render_failed && !status.writer_error && !status.exception_error) {
        status.success = true;
    }

    return result;
}

} // namespace chronon3d::cli
