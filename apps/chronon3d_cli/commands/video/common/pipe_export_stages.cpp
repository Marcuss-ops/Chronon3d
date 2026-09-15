#include "pipe_export_session_internal.hpp"
#include "temporal_render_bridge.hpp"

#include "../../../utils/video/encoder_config_resolution.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <spdlog/spdlog.h>

#include <cstring>
#include <span>
#include <vector>

namespace chronon3d::cli::detail {

NativeSurfacePrep prepare_frame(
    const RenderLoopContext& ctx,
    RenderSettings& frame_settings,
    Frame current_frame) {
    NativeSurfacePrep prep;
    // `frame_settings` is the job-scoped scratch seeded once from ctx.settings.
    // Every field this function derives is written (or explicitly cleared) on
    // each call, so reusing the scratch across frames cannot leak a previous
    // frame's native-surface contract into this one — the property the old
    // per-frame `prep.video_settings = settings` copy provided implicitly, at
    // the cost of copying the whole RenderSettings on every frame.
    frame_settings.native_video_encode_surface = runtime::kInvalidRenderSurfaceHandle;
    frame_settings.native_video_source_surface = runtime::kInvalidRenderSurfaceHandle;
    frame_settings.native_video_previous_surface = runtime::kInvalidRenderSurfaceHandle;
    const bool explicit_native_graph =
        ctx.opts.gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative;
    const bool plan_requests_native_graph = explicit_native_graph ||
        (ctx.execution_plan
            ? (ctx.execution_plan->composite == media::CompositePath::VulkanGraph &&
               ctx.execution_plan->encode == media::EncodePath::Nvenc &&
               ctx.execution_plan->interop == media::InteropPath::VulkanCuda)
            : (is_native_encoder_transport(ctx.opts.encoder.encoder_backend) &&
               ctx.opts.encoder.hardware_encoder == "nvenc"));
    frame_settings.retain_native_surface_for_video =
        plan_requests_native_graph && ctx.backend.supports_native_video_surface();
    frame_settings.require_native_gpu =
        frame_settings.retain_native_surface_for_video;
    auto* surface_registry = ctx.sw_renderer
        ? ctx.sw_renderer->execution_context().images.surfaces : nullptr;
    if (!surface_registry || !frame_settings.retain_native_surface_for_video) {
        return prep;
    }

    prep.slot = ctx.execution_slots.acquire(ctx.opts.cancellation_token);
    if (!prep.slot.valid()) return prep;

    auto& execution_slot = prep.slot.slot();
    if (!ctx.device_runtime) {
        spdlog::error("[video] native surface acquisition requires a device runtime");
        prep.slot.release();
        return prep;
    }
    std::string reason;
    if (!ctx.device_runtime->acquire_slot_surfaces(
            execution_slot, *surface_registry, ctx.backend,
            static_cast<std::uint32_t>(ctx.compiled.composition->width()),
            static_cast<std::uint32_t>(ctx.compiled.composition->height()), reason)) {
        spdlog::error("[video] failed to acquire native surfaces for slot {}: {}",
                      execution_slot.slot_id, reason);
        prep.slot.release();
        return prep;
    }
    frame_settings.native_video_encode_surface = execution_slot.native_surface;
    // FullGraph has one physical encode backing per slot. The source role is
    // an alias used by the writer contract, never a second allocation.
    frame_settings.native_video_source_surface = execution_slot.native_surface;
    frame_settings.native_video_previous_surface =
        ctx.execution_slots.last_completed_surface();
    return prep;
}

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
        ctx.sw_renderer, ctx.opts.cancellation_token, nullptr);
    const auto frame_t1 = profiling::now();
    const FrameProfileSample after = sample_frame_profile(ctx);
    out.frame_ms = profiling::duration_ms(frame_t0, frame_t1);
    out.timing = project_frame_timings(before, after, out.frame_ms);

    out.dirty_ratio = ctx.sw_renderer->last_dirty_area_ratio();
    out.dirty_rect_enabled = ctx.sw_renderer->last_dirty_rect_enabled();
    out.dirty_rect = ctx.sw_renderer->last_dirty_rect();
    // NOTE: `dirty_rect` above is the CPU-side renderer's PREDICTED damage; it
    // feeds diagnostics and the software paths only. It is deliberately not
    // promoted into `out.dirty_regions`, which is the region set for the GPU
    // surface handoff: the encoder's NV12 destination is a pooled frame that
    // does not retain the previous frame, so a partial conversion derived from
    // a prediction publishes a torn frame (background/foreground/watermark
    // clips encoded as a flat field while the graph had rendered every layer).
    // The graph composites the full surface on this lane, so the boundary must
    // convert the full surface. `out.dirty_regions` stays empty until a
    // producer can prove destination continuity
    // (media/video/nv12_region_policy.hpp).
    out.tile_execution_used = ctx.sw_renderer->last_tile_execution_used();
    out.fast_path_reused = ctx.sw_renderer->last_fast_path_reused();
    out.graph_reused = ctx.sw_renderer->last_graph_reused();
    return out;
}

EncodeOutcome encode_frame(
    const RenderLoopContext& ctx,
    std::shared_ptr<Framebuffer> fb,
    const RenderSettings& video_settings,
    std::shared_ptr<FramebufferArena> current_arena,
    NativeSurfacePrep prep,
    Frame current_frame,
    PipeExportStatus& status,
    std::span<const raster::BBox> dirty_regions) {
    EncodeOutcome out;
    const auto wait_t0 = profiling::now();
    auto source_surface = fb ? fb->surface_handle() : runtime::kInvalidRenderSurfaceHandle;
    auto* execution_slot = prep.slot.valid() ? &prep.slot.slot() : nullptr;
    if (source_surface == runtime::kInvalidRenderSurfaceHandle && execution_slot &&
        execution_slot->source_surface != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend.is_native_surface_valid(execution_slot->source_surface)) {
        source_surface = execution_slot->source_surface;
    }
    if (execution_slot && execution_slot->native_surface != runtime::kInvalidRenderSurfaceHandle) {
        // Z3 invariant: the graph output and encoder input are the same
        // exportable backing. A distinct source would require a device copy;
        // fail closed so the copy can never silently return to production.
        if (source_surface != runtime::kInvalidRenderSurfaceHandle &&
            source_surface != execution_slot->native_surface) {
            spdlog::error(
                "[video] FullGraph output escaped encode surface at frame {} "
                "source={} encode={}", static_cast<int>(current_frame),
                source_surface, execution_slot->native_surface);
            out.source_residency_failed = true;
            return out;
        }
        source_surface = execution_slot->native_surface;
        execution_slot->source_surface = execution_slot->native_surface;
    }
    auto* surface_registry = ctx.sw_renderer
        ? ctx.sw_renderer->execution_context().images.surfaces : nullptr;
    // When a native encode destination exists, finish_frame_encode() has
    // already copied the rendered source into CUDA/NVENC. Do not require the
    // temporary source handle to survive that copy; this validation is only
    // needed for retained-surface paths without a dedicated encode target.
    const bool require_retained_source =
        video_settings.retain_native_surface_for_video &&
        video_settings.native_video_encode_surface ==
            runtime::kInvalidRenderSurfaceHandle;
    if (surface_registry && require_retained_source) {
        if (source_surface != runtime::kInvalidRenderSurfaceHandle &&
            !ctx.backend.is_native_surface_valid(source_surface)) {
            spdlog::error("[video] native source surface became invalid before frame {} handle={}",
                          static_cast<int>(current_frame), source_surface);
            (void)surface_registry->release(source_surface);
            fb->clear_surface_handle();
            source_surface = runtime::kInvalidRenderSurfaceHandle;
        }
        const auto source_desc = runtime::SurfaceDesc::make(
            static_cast<std::uint32_t>(fb->width()),
            static_cast<std::uint32_t>(fb->height()),
            runtime::PixelFormat::Rgba32Float,
            runtime::ResourceUsage::Storage,
            runtime::LifetimeClass::PipelineSlot);
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
                    spdlog::error(
                        "[video] native upload failed for retained source at frame {} "
                        "(handle={}); dropping to the CPU encode lane",
                        static_cast<int>(current_frame), source_surface);
                    if (!ctx.backend.release_surface(source_surface).ok()) {
                        spdlog::error("[video] release_surface failed for upload-failure handle={}",
                                      source_surface);
                    }
                    if (!surface_registry->release(source_surface)) {
                        spdlog::error("[video] surface_registry release failed for upload-failure handle={}",
                                      source_surface);
                    }
                    source_surface = runtime::kInvalidRenderSurfaceHandle;
                }
            } else {
                spdlog::error(
                    "[video] create_surface failed for retained source at frame {} "
                    "(handle={}); dropping to the CPU encode lane",
                    static_cast<int>(current_frame), source_surface);
                if (!surface_registry->release(source_surface)) {
                    spdlog::error("[video] surface_registry release failed for create-failure handle={}",
                                  source_surface);
                }
                source_surface = runtime::kInvalidRenderSurfaceHandle;
            }
        }
    }
    if (execution_slot && execution_slot->native_surface != runtime::kInvalidRenderSurfaceHandle) {
        const bool submitted =
            execution_slot->transition_interop_state(runtime::InteropFrameState::VulkanRecording) &&
            execution_slot->transition_interop_state(runtime::InteropFrameState::VulkanSubmitted) &&
            execution_slot->transition_interop_state(runtime::InteropFrameState::VulkanComplete);
        if (!submitted) {
            spdlog::error(
                "[video] invalid Vulkan interop lifecycle for slot {} at frame {}",
                execution_slot->slot_id, static_cast<int>(current_frame));
            out.source_residency_failed = true;
            return out;
        }
    }
    out.package.emplace<FullGraphFramePackage>();
    auto& full_package = std::get<FullGraphFramePackage>(out.package);
    full_package.frame_number = current_frame;
    full_package.slot = std::move(prep.slot);
    full_package.cpu_fallback = std::move(fb);
    full_package.cpu_arena = std::move(current_arena);
    full_package.dirty_regions.assign(dirty_regions.begin(), dirty_regions.end());
    ++status.frames_rendered;

    if (execution_slot && execution_slot->native_surface !=
            runtime::kInvalidRenderSurfaceHandle) {
        // Publish encoder ownership before the package becomes visible to the
        // writer.  Otherwise a very fast writer could release the slot and
        // race a stale ownership publication from the producer.
        ctx.execution_slots.mark_encoder_owned(execution_slot->native_surface);
    }
    out.pushed = ctx.queue.push(out.package, ctx.opts.cancellation_token);
    out.wait_ms = profiling::duration_ms(wait_t0, profiling::now());

    if (out.pushed) {
        // Sample the published queue state. Sampling before push understates
        // the actual bounded backlog and can hide a full-ring condition.
        const auto queue_depth = static_cast<std::uint64_t>(ctx.queue.size_approx());
        ctx.pipeline_metrics.queue_depth_sum.fetch_add(
            queue_depth, std::memory_order_relaxed);
        ctx.pipeline_metrics.queue_depth_samples.fetch_add(
            1, std::memory_order_relaxed);
        observe_pipeline_peak(ctx.pipeline_metrics.queue_depth_peak, queue_depth);
        observe_pipeline_peak(
            ctx.pipeline_metrics.render_slots_in_flight_peak,
            static_cast<std::uint64_t>(ctx.execution_slots.busy_count()));
        if (ctx.counters) {
            observe_pipeline_peak(ctx.counters->io_queue_peak_depth, queue_depth);
        }
    }

    if (chronon3d::tracing::TracingActive()) {
        const int64_t in_flight = static_cast<int64_t>(ctx.queue.size_approx()) +
                                  static_cast<int64_t>(ctx.execution_slots.busy_count());
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

void finalize_render_session(PipeExportStatus& status, Frame current_frame, Frame end) {
    if (current_frame == end && !status.cancelled &&
        !status.render_failed && !status.writer_error && !status.exception_error) {
        status.success = true;
    }
}

} // namespace chronon3d::cli::detail
