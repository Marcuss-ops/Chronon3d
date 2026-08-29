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

namespace {

// ── Per-frame profiling snapshots ───────────────────────────────────────────
// Snapshot the cumulative render-phase counters before/after each frame and
// take deltas. Graph execution writes these counters synchronously on the
// render thread, so a before/after pair is a clean per-frame window. Capturing
// every phase in one typed struct (instead of three lambda + a node-lookup
// read) keeps the render-loop sequencing explicit and the projection pure.
struct FrameProfileSample {
    uint64_t timeline_eval_us{0};
    uint64_t text_us{0};
    uint64_t graph_prepare_us{0};
    uint64_t graph_execute_us{0};
    uint64_t compositing_us{0};
    uint64_t effects_us{0};
    uint64_t surface_us{0};
    uint64_t overhead_us{0};
    // Image draw phase
    uint64_t image_draw_us{0};
    uint64_t image_draw_count{0};
    // Text pipeline phases (ms + us mix, matching the originals)
    uint64_t text_shaping_ms{0};
    uint64_t text_bidi_ms{0};
    uint64_t text_layout_ms{0};
    uint64_t text_glyph_lookup_us{0};
    uint64_t text_raster_ms{0};
    uint64_t text_atlas_upload_us{0};
    uint64_t text_draw_us{0};
    // Node cache lookup
    uint64_t node_lookup_us{0};
};

FrameProfileSample sample_frame_profile(const RenderLoopContext& ctx) {
    FrameProfileSample s;
    if (!ctx.counters) {
        return s;
    }
    const auto load = [](const auto& c) {
        return c.load(std::memory_order_relaxed);
    };
    s.timeline_eval_us = load(ctx.counters->timeline_eval_wall_us);
    s.text_us = load(ctx.counters->text_layout_wall_ms) * 1000
              + load(ctx.counters->text_rasterization_wall_ms) * 1000
              + load(ctx.counters->text_shaping_wall_ms) * 1000
              + load(ctx.counters->text_bidi_wall_ms) * 1000
              + load(ctx.counters->glyph_cache_lookup_wall_us)
              + load(ctx.counters->glyph_atlas_upload_wall_us)
              + load(ctx.counters->text_draw_wall_us);
    s.graph_prepare_us = load(ctx.counters->graph_resolve_layers_wall_us)
                       + load(ctx.counters->graph_dirty_rect_wall_us)
                       + load(ctx.counters->graph_build_wall_us);
    s.graph_execute_us = load(ctx.counters->graph_execute_wall_us);
    s.compositing_us = load(ctx.counters->clearnode_wall_ms) * 1000
                     + load(ctx.counters->compositenode_blend_wall_ms) * 1000
                     + load(ctx.counters->compositenode_setup_wall_ms) * 1000
                     + load(ctx.counters->compositenode_copy_wall_ms) * 1000
                     + load(ctx.counters->compositenode_dispatch_wall_ms) * 1000;
    s.effects_us = load(ctx.counters->effect_stack_total_wall_ms) * 1000;
    s.surface_us = load(ctx.counters->framebuffer_acquire_wall_us)
                 + load(ctx.counters->framebuffer_clear_wall_us)
                 + load(ctx.counters->framebuffer_lifetime_wall_us);
    s.overhead_us = load(ctx.counters->node_overhead_wall_us)
                  + load(ctx.counters->node_dispatch_wall_us)
                  + load(ctx.counters->node_schedule_wall_us)
                  + load(ctx.counters->telemetry_emit_wall_us);
    s.image_draw_us = load(ctx.counters->image_draw_wall_us);
    s.image_draw_count = load(ctx.counters->image_draw_count);
    s.text_shaping_ms = load(ctx.counters->text_shaping_wall_ms);
    s.text_bidi_ms = load(ctx.counters->text_bidi_wall_ms);
    s.text_layout_ms = load(ctx.counters->text_layout_wall_ms);
    s.text_glyph_lookup_us = load(ctx.counters->glyph_cache_lookup_wall_us);
    s.text_raster_ms = load(ctx.counters->text_rasterization_wall_ms);
    s.text_atlas_upload_us = load(ctx.counters->glyph_atlas_upload_wall_us);
    s.text_draw_us = load(ctx.counters->text_draw_wall_us);
    s.node_lookup_us = load(ctx.counters->node_cache_lookup_wall_us);
    return s;
}

// Pure projection of a before/after sample pair into the per-frame telemetry
// timing types. No side effects; the render loop feeds this the exact same
// numbers it previously computed inline.
struct FrameTimingProjection {
    double node_lookup_ms{0.0};
    chronon3d::telemetry::FrameRenderBreakdown breakdown{};
    chronon3d::telemetry::FrameImageTiming image_timing{};
    chronon3d::telemetry::FrameTextTiming text_timing{};
};

FrameTimingProjection project_frame_timings(
    const FrameProfileSample& before,
    const FrameProfileSample& after,
    double frame_ms) {
    FrameTimingProjection p;

    p.breakdown.timeline_eval_ms =
        static_cast<double>(after.timeline_eval_us - before.timeline_eval_us) / 1000.0;
    p.breakdown.text_ms =
        static_cast<double>(after.text_us - before.text_us) / 1000.0;
    p.breakdown.graph_prepare_ms =
        static_cast<double>(after.graph_prepare_us - before.graph_prepare_us) / 1000.0;
    p.breakdown.graph_execute_ms =
        static_cast<double>(after.graph_execute_us - before.graph_execute_us) / 1000.0;
    p.breakdown.compositing_ms =
        static_cast<double>(after.compositing_us - before.compositing_us) / 1000.0;
    p.breakdown.effects_ms =
        static_cast<double>(after.effects_us - before.effects_us) / 1000.0;
    p.breakdown.surface_management_ms =
        static_cast<double>(after.surface_us - before.surface_us) / 1000.0;
    p.breakdown.backend_overhead_ms =
        static_cast<double>(after.overhead_us - before.overhead_us) / 1000.0;
    const double accounted_ms = p.breakdown.timeline_eval_ms + p.breakdown.text_ms +
                                p.breakdown.graph_prepare_ms + p.breakdown.graph_execute_ms +
                                p.breakdown.compositing_ms + p.breakdown.effects_ms +
                                p.breakdown.surface_management_ms +
                                p.breakdown.backend_overhead_ms;
    p.breakdown.accounted_cpu_ms = accounted_ms;
    p.breakdown.unaccounted_cpu_ms = std::max(0.0, frame_ms - accounted_ms);
    // animation_eval_ms remains 0.0 — folded into timeline_eval_ms.

    p.image_timing.draw_ms =
        static_cast<double>(after.image_draw_us - before.image_draw_us) / 1000.0;
    p.image_timing.draw_count = after.image_draw_count - before.image_draw_count;

    p.text_timing.shaping_ms =
        static_cast<double>(after.text_shaping_ms - before.text_shaping_ms);
    p.text_timing.bidi_ms =
        static_cast<double>(after.text_bidi_ms - before.text_bidi_ms);
    p.text_timing.layout_ms =
        static_cast<double>(after.text_layout_ms - before.text_layout_ms);
    p.text_timing.glyph_cache_lookup_ms =
        static_cast<double>(after.text_glyph_lookup_us - before.text_glyph_lookup_us) / 1000.0;
    p.text_timing.raster_ms =
        static_cast<double>(after.text_raster_ms - before.text_raster_ms);
    p.text_timing.atlas_upload_ms =
        static_cast<double>(after.text_atlas_upload_us - before.text_atlas_upload_us) / 1000.0;
    p.text_timing.draw_ms =
        static_cast<double>(after.text_draw_us - before.text_draw_us) / 1000.0;

    p.node_lookup_ms =
        static_cast<double>(after.node_lookup_us - before.node_lookup_us) / 1000.0;
    return p;
}

// ── Per-frame stage decomposition ────────────────────────────────────────────
// run_render_loop orchestrates a fixed stage pipeline per frame:
//   prepare_frame -> render_frame -> encode_frame -> commit_frame
// Each stage owns one contiguous concern; the loop keeps only the small set of
// control-flow decisions (cancellation, writer failure, failed render, failed
// push). Locals that must outlive a single stage (the arena package ownership,
// interop slot, native surface handle) flow through explicitly.

struct NativeSurfacePrep {
    RenderSettings video_settings{};
    runtime::RenderSurfaceHandle native_surface{runtime::kInvalidRenderSurfaceHandle};
    runtime::RenderSurfaceHandle source_surface{runtime::kInvalidRenderSurfaceHandle};
    std::size_t interop_slot{FrameInteropRing::kInvalidSlot};
};

// PREPARE: acquire the swap-chained arena for this frame, register the
// fb pool with it, and (for native NVENC backends) prepare the persistent
// encode + source GPU surfaces owned by the interop ring slot. Returns the
// fully-resolved video settings and the native-surface state; the caller owns
// the acquired arena (its lifetime spans the whole frame).
NativeSurfacePrep prepare_frame(
    const RenderLoopContext& ctx,
    const RenderSettings& settings,
    Frame current_frame) {
    NativeSurfacePrep prep;
    prep.video_settings = settings;
    prep.video_settings.retain_native_surface_for_video =
        ctx.opts.encoder.encoder_backend == "native" &&
        ctx.opts.encoder.hardware_encoder == "nvenc" &&
        ctx.backend.supports_native_video_surface();
    prep.video_settings.require_native_gpu =
        prep.video_settings.retain_native_surface_for_video;
    prep.native_surface = runtime::kInvalidRenderSurfaceHandle;
    prep.interop_slot = FrameInteropRing::kInvalidSlot;

    auto* surface_registry = ctx.sw_renderer
        ? &ctx.sw_renderer->runtime().surface_registry() : nullptr;
    if (!surface_registry || !prep.video_settings.retain_native_surface_for_video) {
        return prep;
    }

    prep.interop_slot = ctx.interop_ring.acquire(ctx.opts.cancellation_token);
    if (prep.interop_slot == FrameInteropRing::kInvalidSlot) {
        return prep;
    }

    auto& persistent_native_surface =
        ctx.native_encode_surfaces[prep.interop_slot];
    if (persistent_native_surface == runtime::kInvalidRenderSurfaceHandle) {
        const runtime::SurfaceDesc encode_desc{
            static_cast<std::uint32_t>(ctx.compiled.composition->width()),
            static_cast<std::uint32_t>(ctx.compiled.composition->height()),
            runtime::PixelFormat::Rgba8Unorm,
            runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::FrameTransient,
            static_cast<std::size_t>(ctx.compiled.composition->width()) *
                ctx.compiled.composition->height() * 4};
        persistent_native_surface = surface_registry->create(encode_desc);
        const auto created = ctx.backend.create_video_encode_surface(
            persistent_native_surface, encode_desc);
        if (!created.ok()) {
            spdlog::error("[video] failed to create CUDA encode surface slot {}: {}",
                          prep.interop_slot, created.error().message);
            (void)surface_registry->release(persistent_native_surface);
            persistent_native_surface = runtime::kInvalidRenderSurfaceHandle;
        }
    }
    if (persistent_native_surface != runtime::kInvalidRenderSurfaceHandle) {
        prep.native_surface = persistent_native_surface;
        prep.video_settings.native_video_encode_surface = prep.native_surface;
    } else {
        ctx.interop_ring.release(prep.interop_slot);
        prep.interop_slot = FrameInteropRing::kInvalidSlot;
    }
    if (prep.interop_slot != FrameInteropRing::kInvalidSlot) {
        auto& persistent_source = ctx.native_source_surfaces[prep.interop_slot];
        if (persistent_source == runtime::kInvalidRenderSurfaceHandle) {
            const runtime::SurfaceDesc source_desc{
                static_cast<std::uint32_t>(ctx.compiled.composition->width()),
                static_cast<std::uint32_t>(ctx.compiled.composition->height()),
                runtime::PixelFormat::Rgba32Float,
                runtime::ResourceUsage::Storage,
                runtime::LifetimeClass::JobPersistent,
                static_cast<std::size_t>(ctx.compiled.composition->width()) *
                    ctx.compiled.composition->height() * sizeof(float) * 4};
            persistent_source = surface_registry->create(source_desc);
            const auto created = ctx.backend.create_surface(persistent_source, source_desc);
            if (!created.ok()) {
                spdlog::error(
                    "[video] failed to create native source surface slot {} ({}x{}, format={}): {}",
                    prep.interop_slot,
                    source_desc.width,
                    source_desc.height,
                    static_cast<int>(source_desc.format),
                    created.error().message);
                (void)surface_registry->release(persistent_source);
                persistent_source = runtime::kInvalidRenderSurfaceHandle;
            }
        }
        if (persistent_source != runtime::kInvalidRenderSurfaceHandle) {
            prep.video_settings.native_video_source_surface = persistent_source;
            prep.source_surface = persistent_source;
        }
    }
    return prep;
}

struct RenderOutcome {
    std::shared_ptr<Framebuffer> fb;
    FrameTimingProjection timing{};
    std::chrono::steady_clock::time_point wall_start{};
    double frame_ms{0.0};
    double dirty_ratio{0.0};
    bool dirty_rect_enabled{false};
    std::optional<raster::BBox> dirty_rect;
    bool tile_execution_used{false};
    bool fast_path_reused{false};
    bool graph_reused{false};
};

// RENDER: emit the Perfetto flow hop, execute the composition graph for the
// frame, and project the before/after counter snapshot into per-frame timing.
// Returns the rendered framebuffer plus the pure timing/state projection used
// by the commit stage. I/O only; no loop-scoped counters are mutated here.
RenderOutcome render_frame(
    const RenderLoopContext& ctx,
    const RenderSettings& video_settings,
    Frame current_frame) {
    RenderOutcome out;
    const auto trace_flow = chronon3d::tracing::MakeFlowId(
        ctx.trace_job_id, static_cast<uint64_t>(current_frame));
    CHRONON_TRACE_FLOW_IDS("chronon.frame", "RenderFrame", trace_flow,
        ctx.trace_job_id, static_cast<uint64_t>(current_frame));

    const FrameProfileSample before = sample_frame_profile(ctx);
    const auto frame_t0 = profiling::now();
    out.wall_start = frame_t0;
    out.fb = graph::render_compiled_composition_frame_temporal(
        ctx.backend, ctx.node_cache, video_settings, &ctx.registry,
        ctx.video_decoder, ctx.compiled, current_frame,
        ctx.sw_renderer, ctx.opts.cancellation_token);
    const auto frame_t1 = profiling::now();
    const FrameProfileSample after = sample_frame_profile(ctx);
    out.frame_ms = profiling::duration_ms(frame_t0, frame_t1);
    out.timing = project_frame_timings(before, after, out.frame_ms);

    // P1-20 — direct method calls; no null check (reference is mandatory).
    out.dirty_ratio = ctx.sw_renderer->last_dirty_area_ratio();
    out.dirty_rect_enabled = ctx.sw_renderer->last_dirty_rect_enabled();
    out.dirty_rect = ctx.sw_renderer->last_dirty_rect();
    out.tile_execution_used = ctx.sw_renderer->last_tile_execution_used();
    out.fast_path_reused = ctx.sw_renderer->last_fast_path_reused();
    out.graph_reused = ctx.sw_renderer->last_graph_reused();
    return out;
}

struct EncodeOutcome {
    bool pushed{false};
    // True when the frame's native source surface could not be made resident
    // while the backend requires a native GPU path. Distinct from a failed
    // push: the caller must release the arena and mark render-failed (see
    // the require_native_gpu branch of the encode stage).
    bool source_residency_failed{false};
    RenderFramePackage package;
    double wait_ms{0.0};
    uint64_t node_cache_hits_after{0};
};

// ENCODE (handoff): build the publishable RenderFramePackage (owning the
// framebuffer + arena, referencing the native surfaces), then block on the
// bounded RenderFrameQueue — the bounded queue/ring provide back-pressure.
// Returns the package (caller retains ownership if push failed) plus the
// wait and node-cache readings the commit stage needs.
EncodeOutcome encode_frame(
    const RenderLoopContext& ctx,
    std::shared_ptr<Framebuffer> fb,
    const RenderSettings& video_settings,
    std::shared_ptr<FramebufferArena> current_arena,
    const NativeSurfacePrep& prep,
    Frame current_frame,
    PipeExportStatus& status) {
    EncodeOutcome out;
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
    if (source_surface == runtime::kInvalidRenderSurfaceHandle &&
        prep.source_surface != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend.is_native_surface_valid(prep.source_surface)) {
        source_surface = prep.source_surface;
    }
    auto* surface_registry = ctx.sw_renderer
        ? &ctx.sw_renderer->runtime().surface_registry() : nullptr;
    if (surface_registry && video_settings.retain_native_surface_for_video) {
        if (source_surface != runtime::kInvalidRenderSurfaceHandle &&
            !ctx.backend.is_native_surface_valid(source_surface)) {
            spdlog::error("[video] native source surface became invalid before frame {} handle={}",
                          static_cast<int>(current_frame), source_surface);
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
                out.source_residency_failed = true;
                return out;
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
    out.package = RenderFramePackage{
        .frame_number = current_frame,
        .framebuffer = std::move(fb),
        .arena = std::move(current_arena),
        .backend = &ctx.backend,
        .surface_registry = ctx.sw_renderer
            ? &ctx.sw_renderer->runtime().surface_registry() : nullptr,
        .source_surface = source_surface,
        .native_surface = prep.native_surface,
        .interop_slot = prep.interop_slot,
        .native_surface_ready = prep.native_surface != runtime::kInvalidRenderSurfaceHandle};
    ++status.frames_rendered;

    out.pushed = ctx.queue.push(out.package, ctx.opts.cancellation_token);
    const auto wait_t1 = profiling::now();
    out.wait_ms = profiling::duration_ms(wait_t0, wait_t1);

    if (chronon3d::tracing::TracingActive()) {
        const int64_t in_flight =
            static_cast<int64_t>(ctx.queue.size_approx()) +
            static_cast<int64_t>(ctx.interop_ring.busy_count());
        CHRONON_TRACE_COUNTER("chronon.pipeline", "frames_in_flight", in_flight);
        const auto pool_stats = ctx.sw_renderer->framebuffer_pool()->stats();
        CHRONON_TRACE_COUNTER("chronon.pipeline", "framebuffer_pool_live_mb",
            static_cast<int64_t>(pool_stats.current_bytes >> 20));
        CHRONON_TRACE_COUNTER("chronon.pipeline", "framebuffer_pool_free_count",
            static_cast<int64_t>(pool_stats.available_count));
    }

    if (ctx.counters) {
        ctx.counters->io_queue_push_wait_ms.fetch_add(
            static_cast<uint64_t>(out.wait_ms), std::memory_order_relaxed);
    }

    out.node_cache_hits_after = ctx.node_cache.stats().hits;
    return out;
}

// Post-loop session outcome. The mark_* helpers set success=false on any
// interruption; only a loop that exhausted its full range untouched is marked
// successful.
void finalize_render_session(PipeExportStatus& status, Frame current_frame, Frame end) {
    if (current_frame == end && !status.cancelled &&
        !status.render_failed && !status.writer_error && !status.exception_error) {
        status.success = true;
    }
}

} // namespace

// ── Render loop ─────────────────────────────────────────────────────────────

RenderLoopResult run_render_loop(const RenderLoopContext& ctx) {
    RenderLoopResult result;
    auto& status = result.status;
    const int total = static_cast<int>(ctx.end - ctx.start);
    Frame current_frame = ctx.start;
    const auto loop_t0 = profiling::now();

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

            if (ctx.direct_yuv_program) {
                auto* native_decoder =
                    dynamic_cast<media::NativeVideoFrameDecoder*>(ctx.video_decoder);
                const auto direct_t0 = profiling::now();
                auto direct_frame = native_decoder
                    ? ctx.direct_yuv_program->execute(*native_decoder, current_frame)
                    : nullptr;
                const auto direct_t1 = profiling::now();
                const double frame_ms = profiling::duration_ms(direct_t0, direct_t1);
                result.render_graph_eval_ms += frame_ms;
                if (!direct_frame) {
                    ctx.triple_arena.release(current_arena);
                    mark_pipe_render_failed(status, current_frame);
                    break;
                }

                RenderFramePackage package;
                package.frame_number = current_frame;
                package.arena = std::move(current_arena);
                package.direct_yuv = std::move(direct_frame);
                const auto queue_t0 = profiling::now();
                const bool pushed = ctx.queue.push(
                    package, ctx.opts.cancellation_token);
                const double wait_ms = profiling::duration_ms(
                    queue_t0, profiling::now());
                result.queue_wait_ms += wait_ms;
                ++status.frames_rendered;
                if (!pushed) {
                    ctx.triple_arena.release(std::move(package.arena));
                    if (ctx.writer_failed.load()) mark_pipe_writer_failed(status, current_frame);
                    else mark_pipe_render_failed(status, current_frame);
                    break;
                }
                ++status.frames_enqueued;
                ctx.telemetry_frames.push_back({
                    .frame_number = static_cast<int>(current_frame),
                    .wall_start_ms = profiling::duration_ms(loop_t0, direct_t0),
                    .duration_ms = frame_ms + wait_ms,
                    .cache_hit = true,
                    .dirty_area_ratio = 0.0,
                    .node_lookup_ms = 0.0,
                    .graph_eval_ms = 0.0,
                    .direct_yuv_decode_ms = frame_ms,
                    .queue_wait_ms = wait_ms,
                    .render_breakdown = {},
                    .image_timing = {},
                    .text_timing = {},
                    .dirty_rect_enabled = false,
                    .dirty_rect_x0 = 0,
                    .dirty_rect_y0 = 0,
                    .dirty_rect_x1 = 0,
                    .dirty_rect_y1 = 0,
                    .tile_execution_used = false,
                    .fast_path_reused = true,
                    .graph_reused = true,
                    .program_cache_capacity = 1
                });
                if (ctx.writer_failed.load()) {
                    mark_pipe_writer_failed(status, current_frame);
                    break;
                }
                continue;
            }

            const auto node_cache_hits_before = ctx.node_cache.stats().hits;

            const NativeSurfacePrep prep =
                prepare_frame(ctx, ctx.settings, current_frame);

            RenderOutcome shot =
                render_frame(ctx, prep.video_settings, current_frame);
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
                ctx, shot.fb, prep.video_settings, current_arena, prep,
                current_frame, status);
            result.queue_wait_ms += enc.wait_ms;
            const double wait_ms = enc.wait_ms;

            if (enc.source_residency_failed) {
                ctx.triple_arena.release(current_arena);
                mark_pipe_render_failed(status, current_frame);
                break;
            }

            if (!enc.pushed) {
                // Frame-transient source/encode surfaces are reclaimed by the
                // caller after the writer has joined. Recover the arena so it
                // can be released back to the pool.
                auto arena = std::move(enc.package.arena);
                if (ctx.writer_failed.load()) {
                    mark_pipe_writer_failed(status, current_frame);
                } else if (ctx.opts.cancellation_token &&
                           ctx.opts.cancellation_token->is_cancelled()) {
                    mark_pipe_cancelled(status, current_frame);
                }
                ctx.triple_arena.release(arena);
                ctx.interop_ring.release(enc.package.interop_slot);
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
            // producer iteration. Final job cleanup runs after the writer has
            // joined and retires/releases the remaining transients.

            ++status.frames_enqueued;

            // Real cache-hit signal: fast-path reuse or at least one NodeCache hit.
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

    // All frames rendered without interruption → mark success.
    // The mark_* functions set success=false on failures/breaks.
    finalize_render_session(status, current_frame, ctx.end);

    return result;
}

} // namespace chronon3d::cli
