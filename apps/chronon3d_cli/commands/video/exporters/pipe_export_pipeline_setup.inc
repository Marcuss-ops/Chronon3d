std::unique_ptr<PipeExportSession> setup_pipe_export_session(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const RenderSettings& settings,
    const FfmpegExportOptions& opts,
    Frame start,
    Frame end,
    const chronon3d::CpuBudget& cpu_budget,
    std::shared_ptr<media::VideoRuntimeRegistry> video_runtimes,
    runtime::DeviceScheduler* device_scheduler,
    std::shared_ptr<media::VideoJobExecutionContext> execution)
{
    constexpr size_t kArenaPoolCount = 4;
    auto session = std::make_unique<PipeExportSession>(kArenaPoolCount);
    session->execution_plan = opts.resolved_execution_plan;
    bool direct_yuv_requested =
        opts.resolved_execution_path ==
            FfmpegExportOptions::ResolvedExecutionPath::DirectYuv;

    if (execution && execution->reservation) {
        auto& injected_execution = *execution;
        if (!injected_execution.video_runtimes) {
            spdlog::error("[video] incomplete injected execution context: "
                          "reservation and persistent registry are required");
            return session;
        }
        if (injected_execution.device_id != injected_execution.reservation->device()) {
            spdlog::error("[video] injected device id does not match reservation");
            return session;
        }
        session->device_id = injected_execution.device_id;
        session->device_reservation = std::move(injected_execution.reservation);
    } else if (execution && execution->video_runtimes) {
        session->device_id = 0;
    } else if (device_scheduler) {
        runtime::DeviceSelectionRequirements requirements;
        requirements.resources.compute_units =
            (opts.backend_preference == graph::BackendPreference::GPU ||
             direct_yuv_requested) ? 0.5f : 0.0f;
        requirements.resources.nvenc_sessions =
            opts.encoder.hardware_encoder == "nvenc" ? 1U : 0U;
        requirements.resources.nvdec_sessions =
            (opts.encoder.hardware_encoder == "nvenc" || direct_yuv_requested) ? 1U : 0U;
        requirements.cuda = opts.encoder.hardware_encoder == "nvenc" || direct_yuv_requested;
        requirements.nvenc = opts.encoder.hardware_encoder == "nvenc";
        requirements.nvdec = requirements.resources.nvdec_sessions > 0;
        auto reservation = device_scheduler->reserve(requirements);
        if (!reservation) {
            spdlog::error("[video] DeviceScheduler could not reserve a compatible device");
            return session;
        }
        session->device_id = reservation->device();
        session->device_reservation = std::move(*reservation);
    } else {
        session->device_id = 0;
    }

    if (!video_runtimes && execution) {
        video_runtimes = execution->video_runtimes;
    }
    if (!video_runtimes) {
        spdlog::error("[video] FAIL_CLOSED: no persistent VideoRuntimeRegistry supplied. "
                      "The daemon or CLI must pass its shared registry; a throwaway "
                      "registry would reintroduce per-clip codec/hwdevice churn.");
        return session;
    }
    const auto cuda_ordinal = execution ? execution->cuda_device_ordinal : -1;
    session->device_runtime = video_runtimes->get_or_create(
        session->device_id, nullptr, cuda_ordinal);
    if (!session->device_runtime) {
        spdlog::error("[video] failed to obtain the video device runtime; aborting");
        return session;
    }

    const auto& startup_trace = chronon3d::cli::startup_trace();
    session->startup_breakdown.logger_init_ms = startup_trace.logger_init_ms;
    session->startup_breakdown.cli_bootstrap_ms = startup_trace.cli_bootstrap_ms;
    session->startup_breakdown.cli_parse_ms = startup_trace.cli_parse_ms;
    session->startup_breakdown.composition_registration_ms = startup_trace.composition_registration_ms;
    session->startup_breakdown.plan_read_ms = startup_trace.plan_read_ms;
    session->startup_breakdown.plan_json_parse_ms = startup_trace.plan_json_parse_ms;
    session->startup_breakdown.plan_decode_validate_ms = startup_trace.plan_decode_validate_ms;
    session->startup_breakdown.plan_asset_resolve_ms = startup_trace.plan_asset_resolve_ms;
    session->startup_breakdown.plan_compile_ms = startup_trace.plan_compile_ms;
    session->opts = opts;
    session->original_output_path = opts.output.output;
    const auto final_output_path = std::filesystem::path(opts.output.output);
    auto partial_output_path = final_output_path;
    const auto extension = final_output_path.extension();
    if (extension.empty()) {
        partial_output_path += ".partial";
    } else {
        partial_output_path.replace_filename(
            final_output_path.stem().string() + ".partial" + extension.string());
    }
    session->opts.output.output = partial_output_path.string();
    session->start_frame = start;
    session->end_frame = end;
    session->canvas_width = compiled.composition->width();
    session->canvas_height = compiled.composition->height();
    session->total_frames = static_cast<int64_t>(end - start);
    session->trace_job_id = static_cast<std::uint64_t>(
        std::hash<std::string>{}(session->original_output_path)) ^ profiling::timestamp_ns();
    session->started_at_iso =
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
        chronon3d::telemetry::TelemetryManager::get_current_iso_time();
#else
        "";
#endif

    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    const bool codec_auto = session->opts.encoder.codec == "auto";
    const std::string codec = codec_auto
        ? "libx264"
        : resolve_cli_ffmpeg_codec(session->opts.encoder.codec, session->opts.encoder.hardware_encoder);

    if (!session->opts.variants.empty()) {
        if (session->opts.encoder.encoder_backend == "native") {
            spdlog::error("[video] SIMO variants require the shared CPU-frame encoder path; "
                          "native surface fanout is not yet supported");
            return session;
        }
        std::vector<VariantFanoutEncoder::Child> children;
        children.reserve(session->opts.variants.size());
        for (std::size_t index = 0; index < session->opts.variants.size(); ++index) {
            const auto& variant = session->opts.variants[index];
            if (variant.width == 0 || variant.height == 0) {
                spdlog::error("[video] SIMO variant {} has invalid dimensions", index);
                return session;
            }
            FfmpegExportOptions child_opts = session->opts;
            child_opts.variants.clear();
            child_opts.output.output = variant.output.empty()
                ? session->opts.output.output + ".variant" + std::to_string(index)
                : variant.output;
            if (!variant.codec.empty()) child_opts.encoder.codec = variant.codec;
            if (!variant.pixel_format.empty()) child_opts.pipe.pipe_pixfmt = variant.pixel_format;
            const auto child_codec = child_opts.encoder.codec == "auto"
                ? "libx264"
                : resolve_cli_ffmpeg_codec(child_opts.encoder.codec,
                                           child_opts.encoder.hardware_encoder);
            auto child_encoder = create_video_encoder(child_opts, {
                .device_runtime = session->device_runtime});
            if (!child_encoder) return session;
            auto child_options = make_pipe_options(compiled, child_opts, child_codec, cpu_budget);
            child_options.width = static_cast<int>(variant.width);
            child_options.height = static_cast<int>(variant.height);
            child_options.output_path = child_opts.output.output;
            children.push_back({std::move(child_encoder), std::move(child_options)});
        }
        session->encoder = std::make_unique<VariantFanoutEncoder>(std::move(children));
    } else {
        const auto enc_create_t0 = profiling::now();
        session->encoder = create_video_encoder(session->opts, {
            .device_runtime = session->device_runtime});
        session->startup_breakdown.encoder_create_ms = profiling::duration_ms(enc_create_t0, profiling::now());
    }
    if (!session->encoder) {
        spdlog::error("[video] Failed to create encoder");
        return session;
    }

    if (session->opts.sink.sink_type == VideoSinkType::Ffmpeg ||
        session->opts.sink.sink_type == VideoSinkType::RawFile) {
        if (!ensure_output_directory_exists(session->opts.output.output)) {
            return session;
        }
        for (const auto& variant : session->opts.variants) {
            const auto variant_path = variant.output.empty()
                ? session->opts.output.output : variant.output;
            if (!ensure_output_directory_exists(variant_path)) return session;
        }
    }

    auto pipe_options = make_pipe_options(compiled, session->opts, codec, cpu_budget);
    pipe_options.direct_yuv_mode = direct_yuv_requested;
    pipe_options.audio_start_seconds = static_cast<double>(start.integral()) /
        (opts.output.fps_value() > 0.0 ? opts.output.fps_value() : 30.0);
    pipe_options.audio_end_seconds = static_cast<double>(end.integral()) /
        (opts.output.fps_value() > 0.0 ? opts.output.fps_value() : 30.0);
    if (!session->encoder->open(pipe_options)) {
        spdlog::error("[video] Failed to open encoder");
        return session;
    }
    session->startup_breakdown.encoder_open_hw_ctx_ms = session->encoder->open_hw_ctx_ms();
    session->startup_breakdown.cuda_compositor_warmup_ms = session->encoder->cuda_compositor_warmup_ms();
    session->startup_breakdown.encoder_open_nvenc_ms = session->encoder->open_nvenc_ms();
    session->startup_breakdown.encoder_open_mux_header_ms = session->encoder->open_mux_header_ms();

    if (direct_yuv_requested) {
        session->direct_yuv_session = std::make_unique<DirectYuvSession>();
        auto& direct = *session->direct_yuv_session;
        direct.asset_resolver = std::make_unique<assets::AssetResolver>();
        if (session->opts.assets_root) {
            direct.asset_resolver->mount(*session->opts.assets_root);
        } else if (const char* env_root = std::getenv("CHRONON3D_CLI_ASSETS_ROOT");
                   env_root && *env_root) {
            direct.asset_resolver->mount(std::filesystem::path{env_root});
        }
        session->device_runtime->image_cache().set_asset_resolver(direct.asset_resolver.get());
    }

    if (session->opts.sink.sink_type == VideoSinkType::Ffmpeg) {
        track_pipe_encoder_process(session->opts, *session->encoder, session->sys_metrics);
    }

    if (direct_yuv_requested) {
        const auto input_t0 = profiling::now();
        std::string direct_reason;
        session->direct_yuv_session->executor = media::video::DirectYuvExecutor::prepare(
            compiled, session->device_runtime->image_cache(), session->device_runtime,
            direct_reason);
        session->input_open_ms = profiling::duration_ms(input_t0, profiling::now());
        if (!session->direct_yuv_selected()) {
            if (opts.gpu_hot_path_mode == GpuHotPathMode::Auto) {
                spdlog::info("[direct-yuv] auto candidate rejected; falling back to FullGraph: {}",
                             direct_reason);
                session->device_runtime->image_cache().set_asset_resolver(nullptr);
                session->direct_yuv_session.reset();
                direct_yuv_requested = false;
                session->execution_plan = render_graph_fallback_plan(opts);
            } else {
                spdlog::error("[direct-yuv] REQUIRE_DIRECT_YUV failed closed: {}", direct_reason);
                session->direct_yuv_session->required_but_unavailable = true;
                return session;
            }
        }
        if (session->direct_yuv_selected()) {
            spdlog::info("[direct-yuv] selected for video source '{}'",
                         session->direct_yuv_session->executor->video_path());
        }
    }

    const auto renderer_t0 = profiling::now();
    if (session->direct_yuv_selected()) {
        spdlog::info("[direct-yuv] skipped SoftwareRenderer and RenderRuntime construction");
    } else {
        session->full_graph_session = std::make_unique<FullGraphSession>();
        session->full_graph_session = std::make_unique<FullGraphSession>();
        Config renderer_cfg = Config::from_environment(cpu_budget);
        if (direct_yuv_requested) {
            renderer_cfg.set_backend_preference(graph::BackendPreference::Software);
            spdlog::info("[direct-yuv] RequireDirectYuv active: bypassed Vulkan backend initialization");
        } else {
            renderer_cfg.set_backend_preference(opts.backend_preference);
        }
        renderer_cfg.set_gpu_hot_path_mode(opts.gpu_hot_path_mode);
        session->full_graph_session->renderer = create_renderer(
            registry, settings, std::move(renderer_cfg), session->opts.assets_root,
            &session->engine_init_ms, &session->backend_init_ms);
    }
    const auto renderer_t1 = profiling::now();

    if (session->full_graph_session && session->full_graph_session->renderer &&
        session->full_graph_session->renderer->counters()) {
        const auto setup_ms = static_cast<uint64_t>(profiling::duration_ms(renderer_t0, renderer_t1));
        session->full_graph_session->renderer->counters()->setup_graph_parsing_wall_ms.fetch_add(
            setup_ms, std::memory_order_relaxed);
    }
    SoftwareRenderer* sw_renderer = session->full_graph_session
        ? session->full_graph_session->renderer.get() : nullptr;
    if (session->full_graph_session && sw_renderer) {
        session->full_graph_session->surface_backend = &sw_renderer->backend();
        session->full_graph_session->surface_registry = &sw_renderer->runtime().surface_registry();
    }
    session->startup_ms = profiling::duration_ms(process_start_time(), renderer_t1);
    session->startup_breakdown.renderer_runtime_init_ms = session->engine_init_ms;
#ifdef CHRONON3D_ENABLE_VULKAN
    if (session->full_graph_session && session->full_graph_session->renderer) {
      if (auto* vk_b = dynamic_cast<chronon3d::backends::vulkan::VulkanBackend*>(&session->full_graph_session->renderer->backend())) {
        session->startup_breakdown.vulkan_instance_ms = vk_b->init_instance_ms();
        session->startup_breakdown.vulkan_device_ms = vk_b->init_device_ms();
        session->startup_breakdown.vulkan_pipelines_ms = vk_b->init_pipelines_ms();
      }
    }
#endif

    const auto prep_start = profiling::now();
    if (!session->direct_yuv_selected()) {
        const auto font_t0 = profiling::now();
        const auto preparation = runtime::prepare_render(
            sw_renderer, compiled,
            runtime::RenderPreparationOptions{.warmup_renderer = false});
        session->prepare_breakdown.font_preflight_ms = profiling::duration_ms(font_t0, profiling::now());
        if (!preparation.ok()) {
            spdlog::error("[video] Render preparation FAILED:\n{}", preparation.diagnostic());
            return session;
        }
        session->prepare_timings.accumulate(preparation.timings);
    } else {
        session->prepare_breakdown.font_preflight_ms = 0.0;
        spdlog::info("[direct-yuv] bypassed generic runtime::prepare_render and font preflight");
    }

    if (sw_renderer && sw_renderer->counters()) {
        session->encoder->set_counters(sw_renderer->counters());
        sw_renderer->counters()->video_sink_type_id.store(
            static_cast<uint64_t>(session->opts.sink.sink_type), std::memory_order_relaxed);
    }

    if (!session->direct_yuv_selected()) {
        const auto arena_t0 = profiling::now();
        const size_t arena_size = compute_pipe_arena_size(
            compiled.composition->width(), compiled.composition->height());
        session->full_graph_session->triple_arena =
            std::make_unique<TripleBufferArena>(kArenaPoolCount, arena_size);
        session->prepare_breakdown.triple_arena_alloc_ms = profiling::duration_ms(arena_t0, profiling::now());
    }

    auto writer_ctx = std::unique_ptr<WriterThreadContext>(new WriterThreadContext{
        .queue = session->queue,
        .writer_failed = session->writer_failed,
        .triple_arena = session->full_graph_session
            ? session->full_graph_session->triple_arena.get() : nullptr,
        .encoder = *session->encoder,
        .renderer = sw_renderer,
        .counters = sw_renderer ? sw_renderer->counters() : &session->direct_yuv_session->counters,
        .hot_path_mode = opts.gpu_hot_path_mode,
        .execution_plan = session->execution_plan,
        .writer_encode_us_total = session->writer_encode_us_total,
        .frames_encoded = session->frames_encoded,
        .require_native_gpu =
            opts.backend_preference == graph::BackendPreference::GPU &&
            opts.encoder.encoder_backend == "native" &&
            opts.encoder.hardware_encoder == "nvenc",
        .frame_encoder_telemetry = session->frame_encoder_telemetry,
        .trace_job_id = session->trace_job_id,
    });
    const auto spawn_t0 = profiling::now();
    session->writer_thread = std::thread(run_writer_thread, std::ref(*writer_ctx));
    session->writer_ctx = std::move(writer_ctx);
    session->prepare_breakdown.writer_thread_spawn_ms = profiling::duration_ms(spawn_t0, profiling::now());
    session->setup_prepare_ms = profiling::duration_ms(prep_start, profiling::now());

    return session;
}