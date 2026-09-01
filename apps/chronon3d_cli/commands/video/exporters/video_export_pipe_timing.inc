    pipe_timing::JobTimings timings;
    if (session->direct_yuv_selected()) {
        timings.execution_path = "direct_yuv";
    } else if (is_native) {
        timings.execution_path = "full_graph_native";
    } else {
        timings.execution_path = "full_graph";
    }
    if (session->execution_plan) {
        switch (session->execution_plan->handoff) {
        case media::SurfaceHandoffPath::None:
            timings.surface_handoff_path = "none";
            break;
        case media::SurfaceHandoffPath::Direct:
            timings.surface_handoff_path = "direct";
            break;
        case media::SurfaceHandoffPath::VulkanCopy:
            timings.surface_handoff_path = "vulkan_copy";
            break;
        case media::SurfaceHandoffPath::HostUpload:
            timings.surface_handoff_path = "host_upload";
            break;
        }
    }
    if (counters) {
        if (session->direct_yuv_selected()) {
            counters->nvenc_frames.fetch_add(0, std::memory_order_relaxed);
        }
    }
    timings.job_wall_ms = wall_time_ms;
    timings.process_wall_ms = profiling::duration_ms(process_start_time(), wall_t0);
    timings.measurement_kind = chronon3d::cli::startup_measurement_kind_name(
        chronon3d::cli::startup_trace().measurement_kind);
    timings.prepare_ms = profiling::duration_ms(setup_t0, warmup_t1);
    timings.engine_init_ms = session->engine_init_ms;
    timings.backend_init_ms = session->backend_init_ms;
    timings.render_loop_wall_ms = loop_output.render_ms;
    timings.encoder_finalize_ms = encode_ms;
    if (nvml_stats.sample_count > 0) {
        timings.hardware.gpu_utilization_avg = nvml_stats.gpu_utilization_avg;
        timings.hardware.gpu_utilization_peak = nvml_stats.gpu_utilization_peak;
        timings.hardware.nvdec_utilization_avg = nvml_stats.nvdec_utilization_avg;
        timings.hardware.nvdec_utilization_peak = nvml_stats.nvdec_utilization_peak;
        timings.hardware.nvenc_utilization_avg = nvml_stats.nvenc_utilization_avg;
        timings.hardware.nvenc_utilization_peak = nvml_stats.nvenc_utilization_peak;
        timings.hardware.memory_utilization_avg = nvml_stats.memory_utilization_avg;
        timings.hardware.vram_used_peak_mb = nvml_stats.vram_used_peak_mb;
        timings.hardware.vram_total_mb = nvml_stats.vram_total_mb;
    }
    if (is_native) timings.mux_finalize_ms = close_result.native_trailer_ms;
    if (is_native) {
        timings.encoder.submit_cpu_ms = close_result.native_send_ms;
        timings.encoder.backpressure_wait_ms = close_result.native_backpressure_ms;
        timings.encoder.cuda_pending_peak = close_result.native_cuda_pending_peak;
        timings.encoder.cuda_backpressure_wait_count =
            close_result.native_cuda_backpressure_wait_count;
        timings.encoder.flush_ms = close_result.native_flush_ms;
        timings.encoder.packet_receive_ms = close_result.native_receive_ms;
        timings.encoder.mux_packet_ms = close_result.native_mux_ms;
    } else {
        if (counters) {
            auto* c = counters;
            timings.encoder.pipe_write_cpu_ms = static_cast<double>(
                c->pipe_write_cpu_wall_us.load(std::memory_order_relaxed)) / 1000.0;
            timings.encoder.pipe_backpressure_wait_ms = static_cast<double>(
                c->pipe_backpressure_wait_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        }
    }
    timings.output_finalize_ms = result.output_finalize_ms;
    timings.validation_ms = result.validation_ms;
    timings.ffprobe_ms = result.ffprobe_ms;
    timings.sha256_ms = result.sha256_ms;
    timings.target_fps = session->opts.output.fps_value();
    timings.target_fps_num = session->opts.output.frame_rate().numerator;
    timings.target_fps_den = session->opts.output.frame_rate().denominator;
    timings.prepare = session->prepare_timings;
    timings.plan_compile_ms = session->prepare_timings.plan_compile_ms;
    timings.graph_compile_ms = session->prepare_timings.graph_compile_ms;
    if (counters) {
        const auto* c = counters;
        timings.gpu.gpu_native_surface_frames = c->gpu_native_surface_frames.load(std::memory_order_relaxed);
        timings.gpu.gpu_native_encode_frames = c->gpu_native_encode_frames.load(std::memory_order_relaxed);
        timings.gpu.nv12_to_rgba_frames = c->nv12_to_rgba_frames.load(std::memory_order_relaxed);
        timings.gpu.rgba_to_nv12_frames = c->rgba_to_nv12_frames.load(std::memory_order_relaxed);
        timings.gpu.gpu_surface_copy_frames = c->gpu_surface_copy_frames.load(std::memory_order_relaxed);
        timings.gpu.cpu_pixel_readback_count = c->cpu_pixel_readback_count.load(std::memory_order_relaxed);
        timings.gpu.cpu_pixel_readback_bytes = c->cpu_pixel_readback_bytes.load(std::memory_order_relaxed);
        timings.gpu.native_surface_promotion_count = c->native_surface_promotion_count.load(std::memory_order_relaxed);
        timings.gpu.native_surface_promotion_bytes = c->native_surface_promotion_bytes.load(std::memory_order_relaxed);
        timings.gpu.native_surface_promotion_wall_us = c->native_surface_promotion_wall_us.load(std::memory_order_relaxed);
        timings.gpu.native_surface_empty_create_count = c->native_surface_empty_create_count.load(std::memory_order_relaxed);
        timings.gpu.native_surface_reuse_count = c->native_surface_reuse_count.load(std::memory_order_relaxed);
        timings.gpu.video_pipe_fallback_frames = c->video_pipe_fallback_frames.load(std::memory_order_relaxed);
        timings.gpu.video_native_fallback_frames = c->video_native_fallback_frames.load(std::memory_order_relaxed);
        timings.gpu.gpu_surface_create_failures = c->gpu_surface_create_failures.load(std::memory_order_relaxed);
        timings.gpu.gpu_encode_failures = c->gpu_encode_failures.load(std::memory_order_relaxed);
        timings.gpu.frame_slot_wait_count = c->frame_slot_wait_count.load(std::memory_order_relaxed);
        timings.gpu.frame_slot_wait_us = c->frame_slot_wait_us.load(std::memory_order_relaxed);
        timings.gpu.cuda_vulkan_wait_count = c->cuda_vulkan_wait_count.load(std::memory_order_relaxed);
        timings.gpu.cuda_vulkan_wait_submit_us = c->cuda_vulkan_wait_submit_us.load(std::memory_order_relaxed);
        timings.gpu.cuda_vulkan_signal_count = c->cuda_vulkan_signal_count.load(std::memory_order_relaxed);
        timings.gpu.cuda_vulkan_signal_submit_us = c->cuda_vulkan_signal_submit_us.load(std::memory_order_relaxed);
        timings.gpu.cuda_composite_frames = c->cuda_composite_frames.load(std::memory_order_relaxed);
        timings.gpu.cuda_composite_wall_us = c->cuda_composite_wall_us.load(std::memory_order_relaxed);
        timings.gpu.cuda_encode_queue_peak = c->cuda_encode_queue_peak.load(std::memory_order_relaxed);
        timings.gpu.cuda_encode_event_wait_count = c->cuda_encode_event_wait_count.load(std::memory_order_relaxed);
        timings.gpu.cuda_encode_event_wait_us = c->cuda_encode_event_wait_us.load(std::memory_order_relaxed);
        timings.gpu.encoder_staging_copy_bytes = c->encoder_staging_copy_bytes.load(std::memory_order_relaxed);
    }
    if (counters) {
        auto* c = counters;
        timings.image_draw_ms = static_cast<double>(
            c->image_draw_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.image_draw_count = c->image_draw_count.load(std::memory_order_relaxed);
        timings.text.font_resolve_ms = static_cast<double>(
            c->font_resolve_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.text.shaping_ms = static_cast<double>(
            c->text_shaping_wall_ms.load(std::memory_order_relaxed));
        timings.text.bidi_ms = static_cast<double>(
            c->text_bidi_wall_ms.load(std::memory_order_relaxed));
        timings.text.layout_ms = static_cast<double>(
            c->text_layout_wall_ms.load(std::memory_order_relaxed));
        timings.text.glyph_cache_lookup_ms = static_cast<double>(
            c->glyph_cache_lookup_wall_us.load(std::memory_order_relaxed)) / 1000.0;
