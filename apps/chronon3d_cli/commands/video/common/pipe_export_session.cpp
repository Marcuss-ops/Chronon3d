#include "pipe_export_session.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include "temporal_render_bridge.hpp"
#include <spdlog/spdlog.h>

#include <optional>
#include <cstring>
#include <span>
#include <thread>

namespace chronon3d::cli {


// ── Render loop ─────────────────────────────────────────────────────────────

RenderLoopResult run_render_loop(const RenderLoopContext& ctx) {
    RenderLoopResult result;
    auto& status = result.status;
    const int total = static_cast<int>(ctx.end - ctx.start);
    Frame current_frame = ctx.start;
    const auto loop_t0 = profiling::now();

    // Per-frame architectural breakdown: snapshot the cumulative render-phase
    // counters before/after each frame and take deltas.  Graph execution
    // writes these counters synchronously on the render thread, so a
    // before/after read is a clean per-frame window.
    struct PhaseCounters {
        uint64_t timeline_eval{0};
        uint64_t text{0};
        uint64_t graph_prepare{0};
        uint64_t graph_execute{0};
        uint64_t compositing{0};
        uint64_t effects{0};
        uint64_t surface{0};
        uint64_t overhead{0};
    };
    const auto snapshot_phases = [&ctx]() {
        PhaseCounters s;
        if (!ctx.counters) return s;
        const auto load = [](const auto& c) {
            return c.load(std::memory_order_relaxed);
        };
        s.timeline_eval = load(ctx.counters->timeline_eval_wall_ms);
        s.text = load(ctx.counters->text_layout_wall_ms)
               + load(ctx.counters->text_rasterization_wall_ms)
               + load(ctx.counters->text_shaping_wall_ms)
               + load(ctx.counters->text_bidi_wall_ms);
        s.graph_prepare = load(ctx.counters->graph_resolve_layers_wall_ms)
                        + load(ctx.counters->graph_dirty_rect_wall_ms)
                        + load(ctx.counters->graph_build_wall_ms);
        s.graph_execute = load(ctx.counters->graph_execute_wall_ms);
        s.compositing = load(ctx.counters->clearnode_wall_ms)
                      + load(ctx.counters->compositenode_blend_wall_ms)
                      + load(ctx.counters->compositenode_setup_wall_ms)
                      + load(ctx.counters->compositenode_copy_wall_ms)
                      + load(ctx.counters->compositenode_dispatch_wall_ms);
        s.effects = load(ctx.counters->effect_stack_total_wall_ms);
        s.surface = load(ctx.counters->framebuffer_acquire_wall_ms)
                  + load(ctx.counters->framebuffer_clear_wall_ms)
                  + load(ctx.counters->framebuffer_lifetime_wall_ms);
        s.overhead = load(ctx.counters->node_overhead_wall_ms)
                   + load(ctx.counters->node_dispatch_wall_ms)
                   + load(ctx.counters->node_schedule_wall_ms)
                   + load(ctx.counters->telemetry_emit_wall_ms);
        return s;
    };
    struct ImageDrawCounters {
        uint64_t draw_us{0};
        uint64_t draw_count{0};
    };
    const auto snapshot_image = [&ctx]() {
        ImageDrawCounters s;
        if (!ctx.counters) return s;
        s.draw_us = ctx.counters->image_draw_wall_us.load(std::memory_order_relaxed);
        s.draw_count = ctx.counters->image_draw_count.load(std::memory_order_relaxed);
        return s;
    };
    // Per-frame text pipeline: shaping/bidi/layout are prepare-only in
    // steady state (≈ 0 delta per frame); glyph lookup / raster / atlas
    // upload / draw accumulate on the render thread each frame.
    struct TextCounters {
        uint64_t shaping_ms{0};
        uint64_t bidi_ms{0};
        uint64_t layout_ms{0};
        uint64_t glyph_lookup_us{0};
        uint64_t raster_ms{0};
        uint64_t atlas_upload_us{0};
        uint64_t draw_us{0};
    };
    const auto snapshot_text = [&ctx]() {
        TextCounters s;
        if (!ctx.counters) return s;
        const auto load = [](const auto& c) {
            return c.load(std::memory_order_relaxed);
        };
        s.shaping_ms = load(ctx.counters->text_shaping_wall_ms);
        s.bidi_ms = load(ctx.counters->text_bidi_wall_ms);
        s.layout_ms = load(ctx.counters->text_layout_wall_ms);
        s.glyph_lookup_us = load(ctx.counters->glyph_cache_lookup_wall_us);
        s.raster_ms = load(ctx.counters->text_rasterization_wall_ms);
        s.atlas_upload_us = load(ctx.counters->glyph_atlas_upload_wall_us);
        s.draw_us = load(ctx.counters->text_draw_wall_us);
        return s;
    };

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
            // P1-20 — sw_renderer is a non-nullable reference; no null check.
            ctx.sw_renderer->framebuffer_pool()->set_arena(current_arena);

            const auto node_cache_hits_before = ctx.node_cache.stats().hits;

            const PhaseCounters phase_before = snapshot_phases();
            const ImageDrawCounters image_before = snapshot_image();
            const TextCounters text_before = snapshot_text();
            const uint64_t node_lookup_before = ctx.counters
                ? ctx.counters->node_cache_lookup_wall_us.load(std::memory_order_relaxed) : 0;
            const auto frame_t0 = profiling::now();
            RenderSettings video_settings = ctx.settings;
            video_settings.retain_native_surface_for_video =
                ctx.opts.encoder.encoder_backend == "native" &&
                ctx.opts.encoder.hardware_encoder == "nvenc" &&
                ctx.backend.supports_native_video_surface();
            video_settings.require_native_gpu = video_settings.retain_native_surface_for_video;
            auto native_surface = runtime::kInvalidRenderSurfaceHandle;
            auto interop_slot = FrameInteropRing::kInvalidSlot;
            auto* surface_registry = ctx.sw_renderer
                ? &ctx.sw_renderer->runtime().surface_registry() : nullptr;
            if (surface_registry && video_settings.retain_native_surface_for_video) {
                interop_slot = ctx.interop_ring.acquire(ctx.opts.cancellation_token);
                if (interop_slot != FrameInteropRing::kInvalidSlot) {
                    auto& persistent_native_surface =
                        ctx.native_encode_surfaces[interop_slot];
                    if (persistent_native_surface == runtime::kInvalidRenderSurfaceHandle) {
                        const runtime::SurfaceDesc encode_desc{
                            static_cast<std::uint32_t>(ctx.compiled.definition->composition.width),
                            static_cast<std::uint32_t>(ctx.compiled.definition->composition.height),
                            runtime::PixelFormat::Rgba8Unorm,
                            runtime::ResourceUsage::Storage,
                            runtime::LifetimeClass::FrameTransient,
                            static_cast<std::size_t>(ctx.compiled.definition->composition.width) *
                                ctx.compiled.definition->composition.height * 4};
                        persistent_native_surface = surface_registry->create(encode_desc);
                        const auto created = ctx.backend.create_video_encode_surface(
                            persistent_native_surface, encode_desc);
                        if (!created.ok()) {
                            spdlog::error("[video] failed to create CUDA encode surface slot {}: {}",
                                          interop_slot, created.error().message);
                            (void)surface_registry->release(persistent_native_surface);
                            persistent_native_surface = runtime::kInvalidRenderSurfaceHandle;
                        }
                    }
                    if (persistent_native_surface != runtime::kInvalidRenderSurfaceHandle) {
                        native_surface = persistent_native_surface;
                        video_settings.native_video_encode_surface = native_surface;
                    } else {
                        ctx.interop_ring.release(interop_slot);
                        interop_slot = FrameInteropRing::kInvalidSlot;
                    }
                    if (interop_slot != FrameInteropRing::kInvalidSlot) {
                        auto& persistent_source = ctx.native_source_surfaces[interop_slot];
                        if (persistent_source == runtime::kInvalidRenderSurfaceHandle) {
                            const runtime::SurfaceDesc source_desc{
                                static_cast<std::uint32_t>(ctx.compiled.definition->composition.width),
                                static_cast<std::uint32_t>(ctx.compiled.definition->composition.height),
                                runtime::PixelFormat::Rgba32Float,
                                runtime::ResourceUsage::Storage,
                                runtime::LifetimeClass::JobPersistent,
                                static_cast<std::size_t>(ctx.compiled.definition->composition.width) *
                                    ctx.compiled.definition->composition.height * sizeof(float) * 4};
                            persistent_source = surface_registry->create(source_desc);
                            const auto created = ctx.backend.create_surface(persistent_source, source_desc);
                            if (!created.ok()) {
                                (void)surface_registry->release(persistent_source);
                                persistent_source = runtime::kInvalidRenderSurfaceHandle;
                            }
                        }
                        if (persistent_source != runtime::kInvalidRenderSurfaceHandle) {
                            video_settings.native_video_source_surface = persistent_source;
                        }
                    }
                }
            }
            auto fb = graph::render_compiled_composition_frame_temporal(
                ctx.backend, ctx.node_cache, video_settings, &ctx.registry,
                ctx.video_decoder, ctx.compiled, current_frame,
                ctx.sw_renderer, ctx.opts.cancellation_token);
            const auto frame_t1 = profiling::now();
            const PhaseCounters phase_after = snapshot_phases();
            const ImageDrawCounters image_after = snapshot_image();
            const TextCounters text_after = snapshot_text();
            const uint64_t node_lookup_after = ctx.counters
                ? ctx.counters->node_cache_lookup_wall_us.load(std::memory_order_relaxed) : 0;
            const double frame_ms =
                profiling::duration_ms(frame_t0, frame_t1);
            result.render_graph_eval_ms += frame_ms;

            chronon3d::telemetry::FrameRenderBreakdown breakdown;
            breakdown.timeline_eval_ms =
                static_cast<double>(phase_after.timeline_eval - phase_before.timeline_eval);
            breakdown.text_ms =
                static_cast<double>(phase_after.text - phase_before.text);
            breakdown.graph_prepare_ms =
                static_cast<double>(phase_after.graph_prepare - phase_before.graph_prepare);
            breakdown.graph_execute_ms =
                static_cast<double>(phase_after.graph_execute - phase_before.graph_execute);
            breakdown.compositing_ms =
                static_cast<double>(phase_after.compositing - phase_before.compositing);
            breakdown.effects_ms =
                static_cast<double>(phase_after.effects - phase_before.effects);
            breakdown.surface_management_ms =
                static_cast<double>(phase_after.surface - phase_before.surface);
            breakdown.backend_overhead_ms =
                static_cast<double>(phase_after.overhead - phase_before.overhead);
            // animation_eval_ms remains 0.0 — folded into timeline_eval_ms.

            chronon3d::telemetry::FrameImageTiming image_timing;
            image_timing.draw_ms =
                static_cast<double>(image_after.draw_us - image_before.draw_us) / 1000.0;
            image_timing.draw_count = image_after.draw_count - image_before.draw_count;

            chronon3d::telemetry::FrameTextTiming text_timing;
            text_timing.shaping_ms =
                static_cast<double>(text_after.shaping_ms - text_before.shaping_ms);
            text_timing.bidi_ms =
                static_cast<double>(text_after.bidi_ms - text_before.bidi_ms);
            text_timing.layout_ms =
                static_cast<double>(text_after.layout_ms - text_before.layout_ms);
            text_timing.glyph_cache_lookup_ms =
                static_cast<double>(text_after.glyph_lookup_us - text_before.glyph_lookup_us) / 1000.0;
            text_timing.raster_ms =
                static_cast<double>(text_after.raster_ms - text_before.raster_ms);
            text_timing.atlas_upload_ms =
                static_cast<double>(text_after.atlas_upload_us - text_before.atlas_upload_us) / 1000.0;
            text_timing.draw_ms =
                static_cast<double>(text_after.draw_us - text_before.draw_us) / 1000.0;

            const double node_lookup_ms =
                static_cast<double>(node_lookup_after - node_lookup_before) / 1000.0;

            if (ctx.counters) {
                ctx.counters->video_graph_eval_wall_ms.fetch_add(
                    static_cast<uint64_t>(frame_ms), std::memory_order_relaxed);
            }

            // P1-20 — direct method calls; no null check (reference is mandatory).
            const double dirty_ratio =
                ctx.sw_renderer->last_dirty_area_ratio();
            const bool dirty_rect_enabled =
                ctx.sw_renderer->last_dirty_rect_enabled();
            const auto dirty_rect =
                ctx.sw_renderer->last_dirty_rect();
            const bool tile_execution_used =
                ctx.sw_renderer->last_tile_execution_used();
            const bool fast_path_reused =
                ctx.sw_renderer->last_fast_path_reused();
            const bool graph_reused =
                ctx.sw_renderer->last_graph_reused();

            if (!fb) {
                ctx.triple_arena.release(current_arena);
                if (ctx.opts.cancellation_token &&
                    ctx.opts.cancellation_token->is_cancelled()) {
                    mark_pipe_cancelled(status, current_frame);
                } else {
                    mark_pipe_render_failed(status, current_frame);
                }
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
                    ctx.counters->graph_skipped_wall_ms_sum.fetch_add(
                        static_cast<uint64_t>(frame_ms * 1000.0), std::memory_order_relaxed);
                } else {
                    ctx.counters->graph_executed_frames.fetch_add(1, std::memory_order_relaxed);
                    ctx.counters->graph_executed_wall_ms_sum.fetch_add(
                        static_cast<uint64_t>(frame_ms * 1000.0), std::memory_order_relaxed);
                }
            }

            // Bounded blocking enqueue: back-pressure is now handled by the
            // queue itself instead of a busy-yield loop.
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

            auto source_surface = fb
                ? fb->surface_handle()
                : runtime::kInvalidRenderSurfaceHandle;
            if (surface_registry && video_settings.retain_native_surface_for_video) {
                if (source_surface != runtime::kInvalidRenderSurfaceHandle &&
                    !ctx.backend.is_native_surface_valid(source_surface)) {
                    // The registry entry may survive after a transient GPU
                    // binding was reclaimed by a copied/cached framebuffer.
                    // Rebuild the source from the current CPU owner; this is
                    // host→GPU upload only and never a GPU readback.
                    (void)surface_registry->release(source_surface);
                    fb->clear_surface_handle();
                    source_surface = runtime::kInvalidRenderSurfaceHandle;
                }
                const runtime::SurfaceDesc source_desc{
                    static_cast<std::uint32_t>(fb->width()),
                    static_cast<std::uint32_t>(fb->height()),
                    runtime::PixelFormat::Rgba32Float,
                    runtime::ResourceUsage::Storage,
                    runtime::LifetimeClass::FrameTransient,
                    static_cast<std::size_t>(fb->width()) * fb->height() * sizeof(float) * 4};
                if (source_surface == runtime::kInvalidRenderSurfaceHandle) {
                    if (video_settings.require_native_gpu) {
                        spdlog::error(
                            "[native-residency] render output has no native source at frame {}",
                            static_cast<int>(current_frame));
                        ctx.triple_arena.release(current_arena);
                        mark_pipe_render_failed(status, current_frame);
                        break;
                    }
                    source_surface = surface_registry->create(source_desc);
                    const auto created = ctx.backend.create_surface(source_surface, source_desc);
                    if (created.ok()) {
                        std::vector<float> packed;
                        std::span<const float> rgba;
                        if (fb->stride() == fb->width()) {
                            static_assert(sizeof(Color) == sizeof(float) * 4);
                            rgba = {reinterpret_cast<const float*>(fb->data()),
                                    static_cast<std::size_t>(fb->width()) * fb->height() * 4};
                        } else {
                            packed.resize(static_cast<std::size_t>(fb->width()) * fb->height() * 4);
                            for (int y = 0; y < fb->height(); ++y) {
                                std::memcpy(
                                    packed.data() + static_cast<std::size_t>(y) * fb->width() * 4,
                                    fb->pixels_row(y),
                                    static_cast<std::size_t>(fb->width()) * sizeof(Color));
                            }
                            rgba = packed;
                        }
                        profiling::GpuUploadProducerScope upload_scope(
                            profiling::GpuUploadProducer::Video);
                        runtime::UploadTicket upload_ticket{};
                        if (!ctx.backend.upload_surface_async(
                                source_surface, source_desc, rgba, upload_ticket).ok()) {
                            (void)ctx.backend.release_surface(source_surface);
                            (void)surface_registry->release(source_surface);
                            source_surface = runtime::kInvalidRenderSurfaceHandle;
                        }
                    } else {
                        (void)surface_registry->release(source_surface);
                        source_surface = runtime::kInvalidRenderSurfaceHandle;
                    }
                }
            }
            RenderFramePackage package{
                .frame_number = current_frame,
                .framebuffer = std::move(fb),
                .arena = std::move(current_arena),
                .backend = &ctx.backend,
                .surface_registry = ctx.sw_renderer
                    ? &ctx.sw_renderer->runtime().surface_registry() : nullptr,
                .source_surface = source_surface,
                .native_surface = native_surface,
                .interop_slot = interop_slot,
                .native_surface_ready = native_surface != runtime::kInvalidRenderSurfaceHandle};
            ++status.frames_rendered;

            bool pushed = ctx.queue.push(package, ctx.opts.cancellation_token);

            const auto wait_t1 = profiling::now();
            const double wait_ms =
                profiling::duration_ms(wait_t0, wait_t1);
            result.queue_wait_ms += wait_ms;

            if (ctx.counters) {
                ctx.counters->io_queue_push_wait_ms.fetch_add(
                    static_cast<uint64_t>(wait_ms), std::memory_order_relaxed);
            }

            if (!pushed) {
                // Frame-transient source/encode surfaces are reclaimed by the
                // caller after the writer has joined.
                // Recover the arena so it can be released back to the pool.
                auto arena = std::move(package.arena);
                if (ctx.writer_failed.load()) {
                    mark_pipe_writer_failed(status, current_frame);
                } else if (ctx.opts.cancellation_token &&
                           ctx.opts.cancellation_token->is_cancelled()) {
                    mark_pipe_cancelled(status, current_frame);
                }
                ctx.triple_arena.release(arena);
                ctx.interop_ring.release(package.interop_slot);
                break;
            }

            // The bounded queue/ring provide backpressure. Do not wait for the
            // writer after every frame: that collapses the render/encode
            // pipeline to depth one and makes Vulkan work serially.
            if (ctx.writer_failed.load(std::memory_order_relaxed)) {
                mark_pipe_writer_failed(status, current_frame);
                break;
            }
            // Retire only resources whose submission fence has completed;
            // the blocking device drain is reserved for final job cleanup.
            ctx.backend.retire_frame_transient_surfaces();
            // Do not release FrameTransient registry handles here.  They are
            // still owned by queued RenderFramePackage instances and may be
            // referenced by the Vulkan/CUDA/NVENC pipeline after this
            // producer iteration. Releasing them here invalidates the native
            // source/output surfaces and forces the next frame through a
            // full CPU->GPU upload. Final job cleanup runs after the writer
            // has joined and retires/releases the remaining transients.

            ++status.frames_enqueued;

            // Real cache-hit signal: fast-path reuse or at least one NodeCache hit.
            const auto node_cache_hits_after = ctx.node_cache.stats().hits;
            const bool cache_hit = fast_path_reused ||
                (node_cache_hits_after > node_cache_hits_before);

            ctx.telemetry_frames.push_back({
                .frame_number = static_cast<int>(current_frame),
                .wall_start_ms = profiling::duration_ms(loop_t0, frame_t0),
                .duration_ms = frame_ms + wait_ms,
                .cache_hit = cache_hit,
                .dirty_area_ratio = dirty_ratio,
                .node_lookup_ms = node_lookup_ms,
                .graph_eval_ms = frame_ms,
                .queue_wait_ms = wait_ms,
                .render_breakdown = breakdown,
                .image_timing = image_timing,
                .text_timing = text_timing,
                .dirty_rect_enabled = dirty_rect_enabled,
                .dirty_rect_x0 = dirty_rect ? dirty_rect->x0 : 0,
                .dirty_rect_y0 = dirty_rect ? dirty_rect->y0 : 0,
                .dirty_rect_x1 = dirty_rect ? dirty_rect->x1 : 0,
                .dirty_rect_y1 = dirty_rect ? dirty_rect->y1 : 0,
                .tile_execution_used = tile_execution_used,
                .fast_path_reused = fast_path_reused,
                .graph_reused = graph_reused,
                .program_cache_capacity = ctx.counters
                    ? static_cast<int>(ctx.counters->program_cache_capacity.load(std::memory_order_relaxed))
                    : 0
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
