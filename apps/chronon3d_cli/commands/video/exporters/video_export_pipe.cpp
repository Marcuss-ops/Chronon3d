#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"
#include "pipe_timing_sidecar.hpp"
#include "../../../utils/process_start.hpp"
#include "../../../utils/telemetry/nvml_sampler.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/telemetry/frame_timing_summary.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <nlohmann/json.hpp>

#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#endif

namespace chronon3d::cli {

PipeExportResult render_and_encode_ffmpeg_pipe(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget,
    std::shared_ptr<media::VideoRuntimeRegistry> video_runtimes,
    runtime::DeviceScheduler* device_scheduler,
    std::shared_ptr<media::VideoJobExecutionContext> execution)
{
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
    if (!close_result.applied_encoder_preset.empty()) {
        timings.encoder_settings.preset = close_result.applied_encoder_preset;
    }
    if (!close_result.applied_encoder_rate_control.empty()) {
        timings.encoder_settings.rate_control = close_result.applied_encoder_rate_control;
    }
    if (close_result.applied_encoder_async_depth > 0) {
        timings.encoder_settings.async_depth = close_result.applied_encoder_async_depth;
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
        timings.text.raster_ms = static_cast<double>(
            c->text_rasterization_wall_ms.load(std::memory_order_relaxed));
        timings.text.atlas_upload_ms = static_cast<double>(
            c->glyph_atlas_upload_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.text.draw_ms = static_cast<double>(
            c->text_draw_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.text.atlas_cache_hits = c->gpu_text_atlas_cache_hits.load(std::memory_order_relaxed);
        timings.text.atlas_cache_misses = c->gpu_text_atlas_cache_misses.load(std::memory_order_relaxed);
        timings.text.atlas_key_bytes_hashed = c->gpu_text_atlas_key_bytes_hashed.load(std::memory_order_relaxed);
        timings.text.atlas_repack_count = c->gpu_text_atlas_repack_count.load(std::memory_order_relaxed);
        timings.text.atlas_repack_bytes = c->gpu_text_atlas_repack_bytes.load(std::memory_order_relaxed);
        timings.text.atlas_upload_count = c->gpu_text_atlas_upload_count.load(std::memory_order_relaxed);
        timings.text.atlas_upload_bytes = c->gpu_text_atlas_upload_bytes.load(std::memory_order_relaxed);
        timings.text.instance_upload_count = c->gpu_text_instance_upload_count.load(std::memory_order_relaxed);
        timings.text.instance_upload_bytes = c->gpu_text_instance_upload_bytes.load(std::memory_order_relaxed);
        timings.cache.node_lookup_ms = static_cast<double>(
            c->node_cache_lookup_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cache.node_cache_hits = c->node_cache_hits.load(std::memory_order_relaxed);
        timings.cache.node_cache_misses = c->node_cache_misses.load(std::memory_order_relaxed);
        timings.cache.glyph_cache_hits = c->glyph_cache_hits.load(std::memory_order_relaxed);
        timings.cache.glyph_cache_misses = c->glyph_cache_misses.load(std::memory_order_relaxed);
        timings.cache.gpu_asset_cache_hits = c->gpu_asset_cache_hits.load(std::memory_order_relaxed);
        timings.cache.gpu_asset_cache_misses = c->gpu_asset_cache_misses.load(std::memory_order_relaxed);
        timings.framebuffer_allocations = c->framebuffer_allocations.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_text = c->framebuffer_alloc_text.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_effect = c->framebuffer_alloc_effect.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_glow = c->framebuffer_alloc_glow.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_video = c->framebuffer_alloc_video.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_graph = c->framebuffer_alloc_graph.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_scratch = c->framebuffer_alloc_scratch.load(std::memory_order_relaxed);
        timings.framebuffer_alloc_unknown = c->framebuffer_alloc_unknown.load(std::memory_order_relaxed);
        timings.gpu.video_decode_frames = c->video_decode_frames.load(std::memory_order_relaxed);
        timings.gpu.video_decode_native_surface_frames = c->video_decode_native_surface_frames.load(std::memory_order_relaxed);
        timings.gpu.video_decode_hw_transfer_frames = c->video_decode_hw_transfer_frames.load(std::memory_order_relaxed);
        timings.gpu.video_decode_software_frames = c->video_decode_software_frames.load(std::memory_order_relaxed);
        timings.gpu.video_decode_native_fallback_frames = c->video_decode_native_fallback_frames.load(std::memory_order_relaxed);
        timings.gpu.video_prefetch_hits = c->video_prefetch_hits.load(std::memory_order_relaxed);
        timings.gpu.video_prefetch_misses = c->video_prefetch_misses.load(std::memory_order_relaxed);
        timings.gpu.video_prefetch_wait_us = c->video_prefetch_wait_us.load(std::memory_order_relaxed);
        timings.gpu.video_prefetch_queue_clear_count = c->video_prefetch_queue_clear_count.load(std::memory_order_relaxed);
        timings.gpu.video_prefetch_queue_depth_peak = c->video_prefetch_queue_depth_peak.load(std::memory_order_relaxed);
        timings.gpu.video_decode_wall_ms = c->video_decode_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.video_decode_hw_transfer_wall_ms = c->video_decode_hw_transfer_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.video_decode_sws_wall_ms = c->video_decode_sws_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.video_decode_framebuffer_wall_ms = c->video_decode_framebuffer_wall_ms.load(std::memory_order_relaxed);
        timings.gpu.hwframe_transfer_to_cpu_frames = c->hwframe_transfer_to_cpu_frames.load(std::memory_order_relaxed);
        timings.gpu.software_color_convert_frames = c->software_color_convert_frames.load(std::memory_order_relaxed);
        timings.gpu.cpu_full_surface_upload_bytes = c->cpu_full_surface_upload_bytes.load(std::memory_order_relaxed);
        timings.gpu.gpu_readback_bytes = c->gpu_readback_bytes.load(std::memory_order_relaxed);
        timings.gpu.nvenc_frames = c->nvenc_frames.load(std::memory_order_relaxed);
        timings.gpu.software_encode_frames = c->software_encode_frames.load(std::memory_order_relaxed);
        timings.gpu.bitstream_copy_frames = c->bitstream_copy_frames.load(std::memory_order_relaxed);
        timings.gpu.vulkan_frames = c->vulkan_frames.load(std::memory_order_relaxed);
        timings.gpu.cpu_readback_frames = c->cpu_readback_frames.load(std::memory_order_relaxed);
        timings.gpu.decode_submit_ms = c->decode_submit_ms.load(std::memory_order_relaxed);
        timings.gpu.decode_wait_ms = c->decode_wait_ms.load(std::memory_order_relaxed);
        timings.gpu.hwframe_transfer_ms = c->hwframe_transfer_ms.load(std::memory_order_relaxed);
        timings.gpu.swscale_ms = c->swscale_ms.load(std::memory_order_relaxed);
        timings.gpu.cpu_pixel_conversion_ms = c->cpu_pixel_conversion_ms.load(std::memory_order_relaxed);
        timings.gpu.full_surface_upload_ms = c->full_surface_upload_ms.load(std::memory_order_relaxed);
        timings.gpu.video_composite_ms = c->video_composite_ms.load(std::memory_order_relaxed);
        timings.gpu.encode_submit_ms = c->encode_submit_ms.load(std::memory_order_relaxed);
        timings.gpu.encode_wait_ms = c->encode_wait_ms.load(std::memory_order_relaxed);

        timings.cpu_breakdown.timeline_eval_ms = static_cast<double>(c->timeline_eval_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.graph_resolve_layers_ms = static_cast<double>(c->graph_resolve_layers_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.graph_dirty_rect_ms = static_cast<double>(c->graph_dirty_rect_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.graph_build_ms = static_cast<double>(c->graph_build_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.graph_execute_ms = static_cast<double>(c->graph_execute_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.compiled_graph_refresh_ms = static_cast<double>(c->compiled_graph_refresh_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.cache_eval_ms = static_cast<double>(c->cache_eval_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.dirty_eval_ms = static_cast<double>(c->dirty_eval_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.input_resolve_ms = static_cast<double>(c->input_resolve_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.predicted_bbox_ms = static_cast<double>(c->predicted_bbox_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.clone_context_ms = static_cast<double>(c->clone_context_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.state_assign_ms = static_cast<double>(c->state_assign_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.framebuffer_acquire_ms = static_cast<double>(c->framebuffer_acquire_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.framebuffer_clear_ms = static_cast<double>(c->framebuffer_clear_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.framebuffer_lifetime_ms = static_cast<double>(c->framebuffer_lifetime_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.node_schedule_ms = static_cast<double>(c->node_schedule_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.node_dispatch_ms = static_cast<double>(c->node_dispatch_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.node_execute_actual_ms = static_cast<double>(c->node_execute_actual_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.node_overhead_ms = static_cast<double>(c->node_overhead_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.telemetry_emit_ms = static_cast<double>(c->telemetry_emit_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.text_layout_ms = static_cast<double>(c->text_layout_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.text_rasterization_ms = static_cast<double>(c->text_rasterization_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.text_shaping_ms = static_cast<double>(c->text_shaping_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.text_bidi_ms = static_cast<double>(c->text_bidi_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.glyph_cache_lookup_ms = static_cast<double>(c->glyph_cache_lookup_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.glyph_atlas_upload_ms = static_cast<double>(c->glyph_atlas_upload_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.text_draw_ms = static_cast<double>(c->text_draw_wall_us.load(std::memory_order_relaxed)) / 1000.0;
        timings.cpu_breakdown.clearnode_ms = static_cast<double>(c->clearnode_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.compositenode_blend_ms = static_cast<double>(c->compositenode_blend_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.effect_stack_total_ms = static_cast<double>(c->effect_stack_total_wall_ms.load(std::memory_order_relaxed));
        timings.cpu_breakdown.graph_executed_frames = c->graph_executed_frames.load(std::memory_order_relaxed);
        timings.cpu_breakdown.graph_reused_frames = c->graph_reused_frames.load(std::memory_order_relaxed);
        timings.cpu_breakdown.fast_path_reused_frames = c->fast_path_reused_frames.load(std::memory_order_relaxed);
        timings.cpu_breakdown.video_source_requested_frames = c->video_source_requested_frames.load(std::memory_order_relaxed);
        timings.cpu_breakdown.video_source_inactive_frames = c->video_source_inactive_frames.load(std::memory_order_relaxed);
        timings.cpu_breakdown.video_source_repeated_frames = c->video_source_repeated_frames.load(std::memory_order_relaxed);
    }
    if (session->renderer_ptr()) {
      const auto atlas_stats = session->renderer_ptr()->runtime().gpu_glyph_atlas().stats();
      timings.text.atlas_cache_hits = atlas_stats.hits;
      timings.text.atlas_cache_misses = atlas_stats.misses;
      timings.text.atlas_upload_count = atlas_stats.page_count;
      timings.text.atlas_upload_bytes = atlas_stats.total_glyph_bytes;
    }
    timings.text.atlas_key_bytes_hashed = 0;
    timings.text.atlas_repack_count = 0;
    timings.text.atlas_repack_bytes = 0;
    timings.cache.image_cache_hits = session->prepare_timings.image_cache_hits;
    timings.cache.image_cache_misses = session->prepare_timings.image_cache_misses;
    timings.cache.font_cache_hits = session->prepare_timings.font_cache_hits;
    timings.cache.font_cache_misses = session->prepare_timings.font_cache_misses;

    if (counters) {
        auto* c = counters;
        timings.runtime.video_runtime_created = c->video_runtime_created.load(std::memory_order_relaxed);
        timings.runtime.video_runtime_reused = c->video_runtime_reused.load(std::memory_order_relaxed);
        timings.runtime.cuda_hwdevice_created = c->cuda_hwdevice_created.load(std::memory_order_relaxed);
        timings.runtime.cuda_hwdevice_reused = c->cuda_hwdevice_reused.load(std::memory_order_relaxed);
        timings.runtime.cuda_frames_cache_hit = c->cuda_frames_cache_hit.load(std::memory_order_relaxed);
        timings.runtime.cuda_frames_cache_miss = c->cuda_frames_cache_miss.load(std::memory_order_relaxed);
        timings.runtime.cuda_image_cache_hit = c->cuda_image_cache_hit.load(std::memory_order_relaxed);
        timings.runtime.cuda_image_cache_miss = c->cuda_image_cache_miss.load(std::memory_order_relaxed);
        timings.gpu.software_fallback_nodes = c->gpu_text_fallback_count.load(std::memory_order_relaxed);
    }
    timings.runtime.encoder_open_nvenc_ms = session->startup_breakdown.encoder_open_nvenc_ms;

    if (session->renderer_ptr() && session->renderer_ptr()->runtime().backend_attached()) {
        std::vector<std::pair<std::string, std::uint64_t>> gpu_counters;
      session->renderer_ptr()->runtime().backend().export_gpu_telemetry_counters(gpu_counters);
        for (const auto& [name, value] : gpu_counters) {
            if (name == "gpu_execute_us") {
                timings.gpu.gpu_execute_ms = static_cast<double>(value) / 1000.0;
            } else if (name == "readback_us") {
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
        // DirectYuvSession now exposes only the executor contract; the old
        // private DirectYuvProgram timing hooks are intentionally unavailable.
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
}

} // namespace chronon3d::cli
