#include "pipe_export_session_internal.hpp"

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif
#include <spdlog/spdlog.h>

#include <atomic>
#include <exception>
#include <memory>

namespace chronon3d::cli {

namespace {

// Wave 4 — job-scoped arena association guard. The pool's arena source is
// attached ONCE per job (arena_set_count ≈ 1/job, never frames/job): the
// provider returns the rotating slot that is active for the frame currently
// being rendered (published by run_render_loop per frame), and the pool
// consults it under its own mutex when a fresh allocation misses the free
// list. Detach is mandatory at the end of the job — the daemon reuses the
// pool across jobs, so a stale provider would dangle past the job that
// installed it.
class JobArenaAssociation {
public:
    explicit JobArenaAssociation(cache::FramebufferPool* pool) : pool_(pool) {
        if (pool_) {
            pool_->attach_arena_provider([this]() {
                return current_frame_arena_.load(std::memory_order_acquire);
            });
            attached_ = true;
        }
    }
    ~JobArenaAssociation() {
        if (pool_ && attached_) {
            pool_->detach_arena_provider();
        }
    }
    JobArenaAssociation(const JobArenaAssociation&) = delete;
    JobArenaAssociation& operator=(const JobArenaAssociation&) = delete;

    /// Publish the frame-scoped rotating arena slot BEFORE the frame's graph
    /// executes: every pool allocation for this frame must come from the same
    /// slot (release-store so worker threads that acquire under the pool mutex
    /// observe it).
    void publish(FramebufferArena* arena) {
        current_frame_arena_.store(arena, std::memory_order_release);
    }

private:
    cache::FramebufferPool* pool_{nullptr};
    bool attached_{false};
    std::atomic<FramebufferArena*> current_frame_arena_{nullptr};
};

} // namespace

RenderLoopResult run_render_loop(const RenderLoopContext& ctx) {
    using namespace detail;

    RenderLoopResult result;
    auto& status = result.status;
    const int total = static_cast<int>(ctx.end - ctx.start);
    Frame current_frame = ctx.start;
    const auto loop_t0 = profiling::now();
    ctx.telemetry_anchor_ns.store(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            loop_t0.time_since_epoch()).count(),
        std::memory_order_release);

    // Wave 4 — pool association is a JOB-lifetime event. The software
    // renderer's pool is attached to a job-scoped provider; per-frame arena
    // rotation (acquire/release of triple-buffer slots) is published through
    // the provider instead of reconfiguring the pool every frame. Guard
    // detaches on every exit (success, cancel, render/writer failure,
    // exception) so a daemon-reused pool never keeps a dangling provider.
    // The shared_ptr is kept alive for the whole job so the raw pointer the
    // association guard holds never dangles before its destructor detaches.
    std::shared_ptr<cache::FramebufferPool> arena_pool_owner =
        ctx.sw_renderer ? ctx.sw_renderer->framebuffer_pool() : nullptr;
    JobArenaAssociation job_arena(arena_pool_owner.get());

    // Job-scoped render-settings scratch. prepare_frame() derives this frame's
    // native-surface contract into it in place, so the frame loop performs four
    // scalar writes instead of copying the whole RenderSettings (three
    // std::string members plus the render budget) once per frame.
    RenderSettings frame_settings = ctx.settings;

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

            const auto frame_loop_t0 = profiling::now();
            auto current_arena = ctx.triple_arena->acquire();
            // Wave 4 — per-frame rotation is a publish to the job-scoped
            // provider (a single atomic store), NOT a pool reconfiguration
            // (no mutex hold, no arena_set_count growth per frame).
            if (current_arena) {
                job_arena.publish(current_arena.get());
            }
            const auto node_cache_hits_before = ctx.node_cache.stats().hits;

            NativeSurfacePrep prep = prepare_frame(ctx, frame_settings, current_frame);
#ifdef CHRONON3D_ENABLE_VULKAN
            if (auto* vulkan = dynamic_cast<backends::vulkan::VulkanBackend*>(&ctx.backend)) {
                vulkan->set_active_frame_index(
                    static_cast<std::uint64_t>(current_frame.integral()));
            }
#endif
            RenderOutcome shot = render_frame(ctx, frame_settings, current_frame);
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
                ctx, shot.fb, frame_settings, current_arena, std::move(prep),
                current_frame, status);
            result.queue_wait_ms += enc.wait_ms;
            const double wait_ms = enc.wait_ms;
            const double frame_loop_ms = profiling::duration_ms(
                frame_loop_t0, profiling::now());
            const double preparation_ms = profiling::duration_ms(
                frame_loop_t0, shot.wall_start);

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
                    const auto registry_live = ctx.sw_renderer->execution_context().images.surfaces
                        ? ctx.sw_renderer->execution_context().images.surfaces->size() : 0;
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
                .wall_start_ms = profiling::duration_ms(loop_t0, frame_loop_t0),
                .duration_ms = frame_loop_ms,
                .preparation_ms = preparation_ms,
                .encode_handoff_ms = wait_ms,
                .cache_hit = cache_hit,
                .dirty_area_ratio = shot.dirty_ratio,
                .node_lookup_ms = node_lookup_ms,
                .graph_eval_ms = frame_ms,
                .queue_wait_ms = wait_ms,
                .render_breakdown = breakdown,
                .image_timing = image_timing,
                .text_timing = text_timing,
                .gpu_timing = shot.timing.gpu_timing,
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
