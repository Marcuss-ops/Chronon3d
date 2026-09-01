                timings.gpu.gpu_readback_ms = static_cast<double>(value) / 1000.0;
            } else if (name == "gpu_submit_cpu_us") {
                timings.gpu.gpu_submit_cpu_ms = static_cast<double>(value) / 1000.0;
            } else if (name == "gpu_wait_cpu_us") {
                timings.gpu.gpu_wait_cpu_ms = static_cast<double>(value) / 1000.0;
            } else if (name == "standalone_wait_count") {
                timings.gpu.standalone_wait_count = value;
            } else if (name == "standalone_wait_us") {
                timings.gpu.standalone_wait_us = value;
            } else if (name == "frame_batch_drain_wait_count") {
                timings.gpu.frame_batch_drain_wait_count = value;
            } else if (name == "frame_batch_drain_wait_us") {
                timings.gpu.frame_batch_drain_wait_us = value;
            } else if (name == "frame_slot_wait_count") {
                timings.gpu.frame_slot_wait_count = value;
            } else if (name == "frame_slot_wait_us") {
                timings.gpu.frame_slot_wait_us = value;
            } else if (name == "gpu_readback_bytes") {
                timings.gpu.gpu_readback_bytes = value;
            } else if (name == "gpu_upload_bytes") {
                timings.gpu.gpu_upload_bytes = value;
            } else if (name == "gpu_upload_full_surface_bytes") {
                timings.gpu.gpu_upload_full_surface_bytes = value;
            } else if (name == "gpu_upload_region_bytes") {
                timings.gpu.gpu_upload_region_bytes = value;
            } else if (name == "native_surface_promotion_count") {
                timings.gpu.native_surface_promotion_count = value;
            } else if (name == "native_surface_promotion_bytes") {
                timings.gpu.native_surface_promotion_bytes = value;
            } else if (name == "native_surface_promotion_wall_us") {
                timings.gpu.native_surface_promotion_wall_us = value;
            } else if (name == "native_surface_empty_create_count") {
                timings.gpu.native_surface_empty_create_count = value;
            } else if (name == "native_surface_reuse_count") {
                timings.gpu.native_surface_reuse_count = value;
            } else if (name.rfind("gpu_upload_", 0) == 0) {
                timings.gpu.upload_breakdown[name] = value;
            } else if (name == "gpu_submissions") {
                timings.gpu.gpu_submissions = value;
            } else if (name == "passes_executed") {
                timings.gpu.passes_executed = value;
            } else if (name == "gpu_nodes") {
                timings.gpu.gpu_nodes = value;
            } else if (name == "gpu_text_fallback_count") {
                timings.gpu.software_fallback_nodes = value;
            }
        }
    }

    const auto proc_start = process_start_time();
    const double startup_ms = session->startup_ms;
    const double input_open_ms = session->input_open_ms;
    const double prepare_exclusive_ms = session->setup_prepare_ms + profiling::duration_ms(warmup_t0, warmup_t1);
    const double render_loop_exclusive_ms = loop_output.render_ms;
    const double mux_finalize_ms = is_native ? close_result.native_trailer_ms : 0.0;
    const double encoder_drain_exclusive_ms = std::max(0.0, encode_ms - mux_finalize_ms);
    const double validation_exclusive_ms = std::max(0.0, result.validation_ms - (result.ffprobe_ms + result.sha256_ms));
    const double ffprobe_exclusive_ms = result.ffprobe_ms;
    const double sha256_exclusive_ms = result.sha256_ms;
    const double output_finalize_exclusive_ms = result.output_finalize_ms;
    const double sidecar_report_ms = 2.0;

    pipe_timing::ExclusiveWallTimeline tl;
    tl.startup_ms = startup_ms;
    tl.input_open_ms = input_open_ms;
    tl.prepare_ms = prepare_exclusive_ms;
    tl.render_loop_ms = render_loop_exclusive_ms;
    tl.encoder_drain_finalize_ms = encoder_drain_exclusive_ms;
    tl.mux_finalize_ms = mux_finalize_ms;
    tl.validation_ms = validation_exclusive_ms;
    tl.ffprobe_ms = ffprobe_exclusive_ms;
    tl.sha256_ms = sha256_exclusive_ms;
    tl.output_finalize_ms = output_finalize_exclusive_ms;
    tl.sidecar_report_ms = sidecar_report_ms;

    const double accounted_sum = startup_ms + input_open_ms + prepare_exclusive_ms +
        render_loop_exclusive_ms + encoder_drain_exclusive_ms + mux_finalize_ms +
        validation_exclusive_ms + ffprobe_exclusive_ms + sha256_exclusive_ms +
        output_finalize_exclusive_ms + sidecar_report_ms;

    const double proc_wall_ms = profiling::duration_ms(proc_start, profiling::now()) + sidecar_report_ms;
    tl.process_wall_ms = proc_wall_ms;
    tl.unaccounted_ms = std::max(0.0, proc_wall_ms - accounted_sum);
    tl.accounted_percent = (accounted_sum / proc_wall_ms) * 100.0;
    timings.exclusive_wall = tl;

    session->startup_breakdown.total_startup_ms = startup_ms;
    const double startup_accounted = session->startup_breakdown.cli_init_ms +
        session->startup_breakdown.logger_init_ms +
        session->startup_breakdown.cli_bootstrap_ms +
        session->startup_breakdown.cli_parse_ms +
        session->startup_breakdown.composition_registration_ms +
        session->startup_breakdown.plan_read_ms +
        session->startup_breakdown.plan_json_parse_ms +
        session->startup_breakdown.plan_decode_validate_ms +
        session->startup_breakdown.plan_asset_resolve_ms +
        session->startup_breakdown.plan_compile_ms +
        session->startup_breakdown.plan_prepare_ms +
        session->startup_breakdown.encoder_create_ms +
        session->startup_breakdown.encoder_open_hw_ctx_ms +
        session->startup_breakdown.cuda_compositor_warmup_ms +
        session->startup_breakdown.encoder_open_nvenc_ms +
        session->startup_breakdown.encoder_open_mux_header_ms +
        session->startup_breakdown.vulkan_instance_ms +
        session->startup_breakdown.vulkan_device_ms +
        session->startup_breakdown.vulkan_pipelines_ms +
        session->startup_breakdown.renderer_runtime_init_ms;
    session->startup_breakdown.accounted_ms = std::min(startup_ms, startup_accounted);
    session->startup_breakdown.unaccounted_ms = std::max(0.0, startup_ms - startup_accounted);
    session->startup_breakdown.phases_observed = 0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.logger_init_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.cli_bootstrap_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.cli_parse_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.composition_registration_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.plan_read_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.plan_json_parse_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.plan_decode_validate_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.plan_asset_resolve_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.plan_compile_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.encoder_create_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.encoder_open_hw_ctx_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.encoder_open_nvenc_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.encoder_open_mux_header_ms > 0.0;
    session->startup_breakdown.phases_observed += session->startup_breakdown.renderer_runtime_init_ms > 0.0;

    spdlog::info(
        "[startup-profile] total={:.2f}ms logger={:.2f}ms cli_bootstrap={:.2f}ms "
        "cli_parse={:.2f}ms composition={:.2f}ms plan_read={:.2f}ms "
        "json_parse={:.2f}ms plan_decode_validate={:.2f}ms asset_resolve={:.2f}ms "
        "plan_compile={:.2f}ms other={:.2f}ms",
        session->startup_breakdown.total_startup_ms,
        session->startup_breakdown.logger_init_ms,
        session->startup_breakdown.cli_bootstrap_ms,
        session->startup_breakdown.cli_parse_ms,
        session->startup_breakdown.composition_registration_ms,
        session->startup_breakdown.plan_read_ms,
        session->startup_breakdown.plan_json_parse_ms,
        session->startup_breakdown.plan_decode_validate_ms,
        session->startup_breakdown.plan_asset_resolve_ms,
        session->startup_breakdown.plan_compile_ms,
        session->startup_breakdown.other_startup_ms);

    session->prepare_breakdown.total_prepare_ms = prepare_exclusive_ms;
    const double prepare_accounted = session->prepare_breakdown.font_preflight_ms +
        session->prepare_breakdown.pool_warmup_ms +
        session->prepare_breakdown.triple_arena_alloc_ms +
        session->prepare_breakdown.writer_thread_spawn_ms;
    session->prepare_breakdown.other_prepare_ms = std::max(0.0, prepare_exclusive_ms - prepare_accounted);

    timings.startup_breakdown = session->startup_breakdown;
    timings.prepare_breakdown = session->prepare_breakdown;
    timings.startup_accounted_ms = session->startup_breakdown.accounted_ms;
    timings.startup_unaccounted_ms = session->startup_breakdown.unaccounted_ms;

    if (decode_stats.decoded_frames > 0) {
        pipe_timing::InternalDecodeProfiling dec_p;
        dec_p.decoded_frames = decode_stats.decoded_frames;
        dec_p.decode_total_ms = decode_stats.decode_total_ms;
        dec_p.demux_read_packet_ms = decode_stats.demux_read_packet_ms;
        dec_p.avcodec_send_packet_ms = decode_stats.avcodec_send_packet_ms;
        dec_p.avcodec_receive_frame_ms = decode_stats.avcodec_receive_frame_ms;
        dec_p.nvdec_wait_ms = decode_stats.nvdec_wait_ms;
        dec_p.cpu_active_ms = decode_stats.demux_read_packet_ms + decode_stats.avcodec_send_packet_ms;
        dec_p.cpu_wait_ms = decode_stats.avcodec_receive_frame_ms;
        if (!decode_stats.frame_durations_ms.empty()) {
            std::vector<double> sorted = decode_stats.frame_durations_ms;
            std::sort(sorted.begin(), sorted.end());
            double sum = 0.0;
            for (double d : sorted) sum += d;
            dec_p.avg_ms_per_frame = sum / sorted.size();
            dec_p.p50_ms_per_frame = sorted[sorted.size() / 2];
            dec_p.p95_ms_per_frame = sorted[static_cast<std::size_t>(sorted.size() * 0.95)];
            dec_p.max_ms_per_frame = sorted.back();
        }
        timings.internal_decode = dec_p;
    }

    pipe_timing::InternalDirectYuvProfiling dyuv_p;
    if (session->direct_yuv_selected()) {
        dyuv_p.input_probe_ms = decode_stats.container_open_ms +
            decode_stats.stream_probe_ms + decode_stats.decoder_open_ms;
        dyuv_p.scene_eval_ms = session->direct_yuv_session->program->scene_eval_ms();
        dyuv_p.watermark_image_load_ms = session->direct_yuv_session->program->watermark_load_ms();
        dyuv_p.watermark_cuda_upload_ms = session->direct_yuv_session->program->watermark_upload_ms();
    }
    dyuv_p.cuda_launch_ms = close_result.direct_yuv_cuda_launch_ms;
    dyuv_p.cuda_event_wait_ms = close_result.direct_yuv_cuda_wait_ms;
    if (counters) {
        dyuv_p.cuda_kernel_total_ms = static_cast<double>(
            counters->cuda_composite_wall_us.load(std::memory_order_relaxed)) / 1000.0;
    }
    timings.internal_direct_yuv = dyuv_p;

    pipe_timing::InternalEncoderProfiling enc_p;
    enc_p.av_hwframe_get_buffer_ms = close_result.encoder_hwframe_get_buffer_ms;
    enc_p.surface_acquire_ms = close_result.encoder_surface_acquire_ms;
    enc_p.nvenc_submit_ms = close_result.encoder_nvenc_submit_ms;
    enc_p.queue_backpressure_wait_ms = close_result.encoder_queue_backpressure_wait_ms;
    enc_p.packet_drain_ms = close_result.encoder_packet_drain_ms;
    enc_p.cpu_active_ms = close_result.encoder_hwframe_get_buffer_ms +
                          close_result.encoder_surface_acquire_ms +
                          close_result.encoder_nvenc_submit_ms +
                          close_result.encoder_packet_drain_ms;
    enc_p.cpu_wait_ms = close_result.encoder_queue_backpressure_wait_ms +
                        close_result.direct_yuv_cuda_wait_ms;
    timings.internal_encoder = enc_p;

    write_frame_timing_sidecar(session->original_output_path,
                               loop_output.telemetry_frames,
                               session->frame_encoder_telemetry,
                               wall_time_ms, loop_output.render_ms, encode_ms,
                               timings, is_native);

    return result;
