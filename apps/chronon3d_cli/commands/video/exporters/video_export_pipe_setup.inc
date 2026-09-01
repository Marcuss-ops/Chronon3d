    const auto wall_t0 = profiling::now();
    const auto setup_t0 = profiling::now();

    runtime::DeviceScheduler local_scheduler;
    if (!device_scheduler) {
        runtime::DeviceCapabilities capabilities;
        capabilities.id = 0;
        capabilities.name = "cli-device-0";
        capabilities.cuda = opts.encoder.hardware_encoder == "nvenc";
        capabilities.nvdec = capabilities.cuda;
        capabilities.nvenc = capabilities.cuda;
        capabilities.nv12 = capabilities.cuda;
        capabilities.p010 = capabilities.cuda;
        capabilities.h264 = capabilities.cuda;
        capabilities.hevc = capabilities.cuda;
        capabilities.av1 = capabilities.cuda;
        local_scheduler.register_device(
            std::move(capabilities),
            runtime::DeviceResourceVector{
                .compute_units = 1.0f,
                .vram_bytes = 0,
                .nvdec_sessions = 1,
                .nvenc_sessions = 1,
                .pcie_bandwidth = 1.0f});
        device_scheduler = &local_scheduler;
    }

    auto session = setup_pipe_export_session(
        registry, compiled, settings, opts, start, end, cpu_budget,
        std::move(video_runtimes), device_scheduler, std::move(execution));
    if (!session || !session->encoder ||
        (!session->renderer_ptr() && !session->direct_yuv_selected()) ||
        (session->direct_yuv_session && session->direct_yuv_session->required_but_unavailable)) {
        return PipeExportResult{};
    }

    if (opts.sink.chunks != 1) {
        spdlog::warn("[video] --chunks is ignored with --ffmpeg-mode pipe in V1");
    }

    const auto warmup_t0 = profiling::now();
    RenderSettings render_opts = settings;
    runtime::RenderPreparationTimings warmup_prepare_timings;
    if (!session->direct_yuv_selected()) {
        const auto warmup_result = warmup_pipe_renderer(
            *session->renderer_ptr(), compiled, opts, &warmup_prepare_timings);
        if (!warmup_result) {
            spdlog::error("[video] export aborted: render preparation failed: {}",
                          warmup_result.error().message);
            return PipeExportResult{};
        }
        session->prepare_timings.accumulate(warmup_prepare_timings);
        warmup_pipe_pool(*session);
    } else {
        spdlog::info("[direct-yuv] bypassed generic warmup_pipe_renderer and 273MB warmup_pipe_pool");
    }
    const auto warmup_t1 = profiling::now();
    session->prepare_breakdown.pool_warmup_ms = session->direct_yuv_selected()
        ? 0.0 : profiling::duration_ms(warmup_t0, warmup_t1);
    session->sys_metrics.sample_cpu_start();

    telemetry::NvmlSampler nvml_sampler;
    nvml_sampler.start(std::chrono::milliseconds(250));

    auto loop_output = run_pipe_export_loop(*session, registry, compiled, render_opts, start, end, opts);
    auto close_result = close_pipe_encoder(*session);

    media::NativeVideoFrameDecoder::DecodeProfilingStats decode_stats{};
    if (session->native_decoder) {
        decode_stats = session->native_decoder->decode_profiling_stats();
    }
    if (session->opts.encoder.encoder_backend == "native" && session->renderer_ptr()) {
        auto& backend = session->renderer_ptr()->runtime().backend();
#ifdef CHRONON3D_ENABLE_VULKAN
        if (auto* vulkan = dynamic_cast<backends::vulkan::VulkanBackend*>(&backend)) {
            (void)vulkan->wait_for_pending_submissions();
        }
#endif
        session->native_decoder.reset();
        backend.release_frame_transient_surfaces();
    }
    nvml_sampler.stop();
    const auto nvml_stats = nvml_sampler.stats();

    const auto wall_t1 = profiling::now();
    const double wall_time_ms = profiling::duration_ms(wall_t0, wall_t1);
    const double encode_ms = profiling::duration_ms(loop_output.render_end, wall_t1);
    const double writer_encode_ms = static_cast<double>(
        session->writer_encode_us_total.load(std::memory_order_relaxed)) / 1000.0;

    spdlog::info("[video] render_ms={:.2f} encode_ms={:.2f} writer_encode_ms={:.2f} wall_ms={:.2f}",
                 loop_output.render_ms, encode_ms, writer_encode_ms, wall_time_ms);
    spdlog::info("[video] FFmpeg queue wait duration: {:.2f} ms", loop_output.loop_result.queue_wait_ms);

    auto result = make_pipe_export_result(*session, loop_output.loop_result, close_result,
                                          loop_output.render_ms, encode_ms, wall_time_ms);
    auto* counters = session->renderer_ptr()
        ? session->renderer_ptr()->counters() : &session->direct_yuv_session->counters;

    record_pipe_telemetry(composition_id, *session, loop_output.loop_result,
                          close_result, loop_output.telemetry_frames,
                          wall_time_ms, loop_output.render_ms, encode_ms, result);

    const bool is_native = (session->opts.encoder.encoder_backend == "native");
