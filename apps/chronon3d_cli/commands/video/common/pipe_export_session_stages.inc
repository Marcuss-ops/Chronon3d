namespace {

struct NativeSurfacePrep {
    RenderSettings video_settings{};
    runtime::FrameExecutionSlotRing::SlotLease slot;
};

NativeSurfacePrep prepare_frame(
    const RenderLoopContext& ctx,
    const RenderSettings& settings,
    Frame current_frame) {
    NativeSurfacePrep prep;
    prep.video_settings = settings;
    // The resolved plan is authoritative when present, but older resolver
    // paths may leave interop unset even though the caller explicitly chose
    // require_gpu_native. In that case failing to acquire slot surfaces makes
    // the graph render CPU-only and the later native-residency gate aborts
    // after expensive frame evaluation. The explicit hot-path requirement is
    // therefore also a native-graph request.
    const bool explicit_native_graph =
        ctx.opts.gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative;
    const bool plan_requests_native_graph = explicit_native_graph ||
        (ctx.execution_plan
            ? (ctx.execution_plan->composite == media::CompositePath::VulkanGraph &&
               ctx.execution_plan->encode == media::EncodePath::Nvenc &&
               ctx.execution_plan->interop == media::InteropPath::VulkanCuda)
            : (ctx.opts.encoder.encoder_backend == "native" &&
               ctx.opts.encoder.hardware_encoder == "nvenc"));
    prep.video_settings.retain_native_surface_for_video =
        plan_requests_native_graph && ctx.backend.supports_native_video_surface();
    prep.video_settings.require_native_gpu =
        prep.video_settings.retain_native_surface_for_video;
    auto* surface_registry = ctx.sw_renderer
        ? &ctx.sw_renderer->runtime().surface_registry() : nullptr;
    if (!surface_registry || !prep.video_settings.retain_native_surface_for_video) {
        return prep;
    }

    prep.slot = ctx.execution_slots.acquire_lease(ctx.opts.cancellation_token);
    if (!prep.slot.valid()) {
        return prep;
    }

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
    prep.video_settings.native_video_encode_surface = execution_slot.native_surface;
    prep.video_settings.native_video_source_surface = execution_slot.source_surface;
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
    bool source_residency_failed{false};
    RenderFramePackage package;
    double wait_ms{0.0};
    uint64_t node_cache_hits_after{0};
};

EncodeOutcome encode_frame(
    const RenderLoopContext& ctx,
    std::shared_ptr<Framebuffer> fb,
    const RenderSettings& video_settings,
    std::shared_ptr<FramebufferArena> current_arena,
    NativeSurfacePrep prep,
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
    auto* execution_slot = prep.slot.valid() ? &prep.slot.slot() : nullptr;
    if (source_surface == runtime::kInvalidRenderSurfaceHandle && execution_slot &&
        execution_slot->source_surface != runtime::kInvalidRenderSurfaceHandle &&
        ctx.backend.is_native_surface_valid(execution_slot->source_surface)) {
        source_surface = execution_slot->source_surface;
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
    if (execution_slot && execution_slot->native_surface != runtime::kInvalidRenderSurfaceHandle) {
        const bool submitted =
            execution_slot->transition_interop_state(
                runtime::InteropFrameState::VulkanRecording) &&
            execution_slot->transition_interop_state(
                runtime::InteropFrameState::VulkanSubmitted) &&
            execution_slot->transition_interop_state(
                runtime::InteropFrameState::VulkanComplete);
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
    ++status.frames_rendered;

    out.pushed = ctx.queue.push(out.package, ctx.opts.cancellation_token);
    const auto wait_t1 = profiling::now();
    out.wait_ms = profiling::duration_ms(wait_t0, wait_t1);

    if (chronon3d::tracing::TracingActive()) {
        const int64_t in_flight =
            static_cast<int64_t>(ctx.queue.size_approx()) +
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

} // namespace
