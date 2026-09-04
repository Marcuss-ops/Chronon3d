#include "pipe_export_session_internal.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif
#include <spdlog/spdlog.h>

#include <exception>

namespace chronon3d::cli {

RenderLoopResult run_render_loop(const RenderLoopContext& ctx) {
    using namespace detail;

    RenderLoopResult result;
    auto& status = result.status;
    const int total = static_cast<int>(ctx.end - ctx.start);
    Frame current_frame = ctx.start;
    const auto loop_t0 = profiling::now();

    try {
        for (; current_frame < ctx.end; ++current_frame) {
            if (ctx.opts.cancellation_token &&
                ctx.opts.cancellation_token->is_cancelled()) {
                mark_pipe_cancelled(status, current_frame);
                break;
            }
            if (ctx.writer_failed.load()) {
                mark_pipe_writer_failed(status, current_frame);
                break;
            }

            const int done_count = static_cast<int>(current_frame - ctx.start + 1);
            if (should_log_pipe_progress(done_count, total)) {
                spdlog::info("[video]   {}/{} frames", done_count, total);
            }

            auto current_arena = ctx.triple_arena->acquire();
            ctx.sw_renderer->framebuffer_pool()->set_arena(current_arena);
            const auto node_cache_hits_before = ctx.node_cache.stats().hits;

            NativeSurfacePrep prep = prepare_frame(ctx, ctx.settings, current_frame);
            RenderOutcome shot = render_frame(ctx, prep.video_settings, current_frame);
            const auto& breakdown = shot.timing.breakdown;
            const auto& image_timing = shot.timing.image_timing;
            const auto& text_timing = shot.timing.text_timing;
            const double node_lookup_ms = shot.timing.node_lookup_ms;
            const double frame_ms = shot.frame_ms;
            result.render_graph_eval_ms += frame_ms;

            if (ctx.counters) {
                ctx.counters->video_graph_eval_wall_ms.fetch_add(
                    static_cast<uint64_t>(frame_ms), std::memory_order_relaxed);
            }

            if (!shot.fb) {
                ctx.triple_arena->release(current_arena);
                if (ctx.opts.cancellation_token &&
                    ctx.opts.cancellation_token->is_cancelled()) {
                    mark_pipe_cancelled(status, current_frame);
                } else {
                    mark_pipe_render_failed(status, current_frame);
                }
                break;
            }

            if (ctx.counters) {
                if (shot.fast_path_reused) {
                    ctx.counters->graph_skipped_frames.fetch_add(1, std::memory_order_relaxed);
                    ctx.counters->graph_skipped_wall_ms_sum.fetch_add(
                        static_cast<uint64_t>(frame_ms * 1000.0), std::memory_order_relaxed);
                } else {
                    ctx.counters->graph_executed_frames.fetch_add(1, std::memory_order_relaxed);
                    ctx.counters->graph_executed_wall_ms_sum.fetch_add(
                        static_cast<uint64_t>(frame_ms * 1000.0), std::memory_order_relaxed);
                }
            }

            EncodeOutcome enc = encode_frame(
                ctx, shot.fb, prep.video_settings, current_arena, std::move(prep),
                current_frame, status);
            result.queue_wait_ms += enc.wait_ms;
            const double wait_ms = enc.wait_ms;

            if (enc.source_residency_failed) {
                ctx.triple_arena->release(current_arena);
                mark_pipe_render_failed(status, current_frame);
                break;
            }

            if (!enc.pushed) {
                auto* full = std::get_if<FullGraphFramePackage>(&enc.package);
                auto arena = full ? std::move(full->cpu_arena) : nullptr;
                if (ctx.writer_failed.load()) {
                    mark_pipe_writer_failed(status, current_frame);
                } else if (ctx.opts.cancellation_token &&
                           ctx.opts.cancellation_token->is_cancelled()) {
                    mark_pipe_cancelled(status, current_frame);
                }
                ctx.triple_arena->release(arena);
                break;
            }

            if (ctx.writer_failed.load(std::memory_order_relaxed)) {
                mark_pipe_writer_failed(status, current_frame);
                break;
            }
            ++status.frames_enqueued;

            if (done_count % 25 == 0) {
#ifdef CHRONON3D_ENABLE_VULKAN
                if (const auto* vulkan = dynamic_cast<const backends::vulkan::VulkanBackend*>(&ctx.backend)) {
                    const auto s = vulkan->stats();
                    const auto registry_live = ctx.sw_renderer->runtime().surface_registry().size();
                    spdlog::info(
                        "[vulkan-lifecycle] frame={} vram_bytes={} live_surfaces={} "
                        "surface_bindings={} registry_live_handles={} physical_images={} "
                        "deferred_releases={} vma_allocations={} surfaces_created={} "
                        "surfaces_released={} queue_packages={} execution_slots_busy={} "
                        "command_batch_active={}",
                        done_count, s.vma_usage_bytes, s.physical_surfaces_live,
                        s.surface_bindings_live, registry_live, s.physical_surfaces_live,
                        s.deferred_surface_release_count, s.vma_allocation_count,
                        s.surface_creations, s.surface_releases, ctx.queue.size_approx(),
                        ctx.execution_slots.busy_count(), s.command_batch_active);
                }
#endif
            }

            const bool cache_hit = shot.fast_path_reused ||
                (enc.node_cache_hits_after > node_cache_hits_before);
            ctx.telemetry_frames.push_back({
                .frame_number = static_cast<int>(current_frame),
                .wall_start_ms = profiling::duration_ms(loop_t0, shot.wall_start),
                .duration_ms = frame_ms + wait_ms,
                .cache_hit = cache_hit,
                .dirty_area_ratio = shot.dirty_ratio,
                .node_lookup_ms = node_lookup_ms,
                .graph_eval_ms = frame_ms,
                .queue_wait_ms = wait_ms,
                .render_breakdown = breakdown,
                .image_timing = image_timing,
                .text_timing = text_timing,
                .dirty_rect_enabled = shot.dirty_rect_enabled,
                .dirty_rect_x0 = shot.dirty_rect ? shot.dirty_rect->x0 : 0,
                .dirty_rect_y0 = shot.dirty_rect ? shot.dirty_rect->y0 : 0,
                .dirty_rect_x1 = shot.dirty_rect ? shot.dirty_rect->x1 : 0,
                .dirty_rect_y1 = shot.dirty_rect ? shot.dirty_rect->y1 : 0,
                .tile_execution_used = shot.tile_execution_used,
                .fast_path_reused = shot.fast_path_reused,
                .graph_reused = shot.graph_reused,
                .program_cache_capacity = ctx.counters
                    ? static_cast<int>(ctx.counters->program_cache_capacity.load(std::memory_order_relaxed))
                    : 0
            });
        }
    } catch (const std::exception& e) {
        mark_pipe_exception(status, current_frame, e);
    }

    finalize_render_session(status, current_frame, ctx.end);
    return result;
}

} // namespace chronon3d::cli
