#include "pipe_export_session.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
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
        const auto pop_t0 = std::chrono::steady_clock::now();
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

        const auto pop_t1 = std::chrono::steady_clock::now();
        const uint64_t dequeue_ms = static_cast<uint64_t>(
            std::chrono::duration<double, std::milli>(pop_t1 - pop_t0).count());

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

            const auto enc_t0 = std::chrono::steady_clock::now();
            if (!ctx.encoder.write_frame(*package.framebuffer)) {
                ctx.writer_failed.store(true);
                return;
            }
            const auto enc_t1 = std::chrono::steady_clock::now();
            const uint64_t enc_us = static_cast<uint64_t>(
                std::chrono::duration<double, std::micro>(enc_t1 - enc_t0).count());
            ctx.writer_encode_us_total.fetch_add(enc_us, std::memory_order_relaxed);
        }

        ctx.triple_arena.release(package.arena);
        package.framebuffer.reset();
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

            const auto frame_t0 = std::chrono::steady_clock::now();
            auto fb = graph::render_composition_frame(
                ctx.backend, ctx.node_cache, ctx.settings, &ctx.registry,
                ctx.video_decoder, ctx.comp, current_frame);
            const auto frame_t1 = std::chrono::steady_clock::now();
            const double frame_ms =
                std::chrono::duration<double, std::milli>(frame_t1 - frame_t0).count();
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

            // High-throughput enqueue
            const auto wait_t0 = std::chrono::steady_clock::now();
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

            if (!status.success) {
                ctx.triple_arena.release(current_arena);
                break;
            }

            ctx.queue.enqueue(
                RenderFramePackage{std::move(fb), std::move(current_arena)});

            const auto wait_t1 = std::chrono::steady_clock::now();
            const double wait_ms =
                std::chrono::duration<double, std::milli>(wait_t1 - wait_t0).count();
            result.queue_wait_ms += wait_ms;

            if (ctx.counters) {
                ctx.counters->io_queue_push_blocked_ms.fetch_add(
                    static_cast<uint64_t>(wait_ms), std::memory_order_relaxed);
            }

            ++status.frames_written;

            result.telemetry_frames.push_back({
                .frame_number = static_cast<int>(current_frame),
                .duration_ms = frame_ms,
                .cache_hit = true,
                .dirty_area_ratio = dirty_ratio,
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

    return result;
}

} // namespace chronon3d::cli
