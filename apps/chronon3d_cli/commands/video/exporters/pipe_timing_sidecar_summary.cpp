// ═══════════════════════════════════════════════════════════════════════════
// pipe_timing_sidecar_summary.cpp — phase 2 of the frame-timing sidecar.
//
// Split out of pipe_timing_sidecar.cpp (pure code move, no schema change):
// the "summary" section (statistics, FPS, budget) and the "job" section
// (wall/process timings + gpu/prepare/image/text/encoder subsections).
// ═══════════════════════════════════════════════════════════════════════════

#include "pipe_timing_sidecar_detail.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstdint>

namespace chronon3d::cli::pipe_timing_detail {

using chronon3d::telemetry::FrameTelemetry;

void build_summary_and_job_sections(
    nlohmann::json& out, const SidecarContext& ctx,
    const std::vector<double>& durations, const pipe_timing::JobTimings& timings) {
    const auto& frames = ctx.frames;
    const auto& enc = ctx.enc;
    const bool is_native = ctx.is_native;
    const double wall_time_ms = ctx.wall_time_ms;
    const double render_ms = ctx.render_ms;

    const auto stats = chronon3d::telemetry::summarize_frame_timings(frames);
    const std::size_t count = frames.size();

    double frame_budget_ms = 0.0;
    uint64_t frames_over_budget = 0;
    if (timings.target_fps && *timings.target_fps > 0) {
        frame_budget_ms = 1000.0 / static_cast<double>(*timings.target_fps);
        for (double value : durations) {
            if (value > frame_budget_ms) ++frames_over_budget;
        }
    }
    const double over_budget_ratio = count > 0
        ? static_cast<double>(frames_over_budget) / static_cast<double>(count) : 0.0;

    double render_only_ms = 0.0;
    for (const auto& f : frames) render_only_ms += render_total_ms(f);
    const double end_to_end_fps = wall_time_ms > 0.0
        ? (1000.0 * static_cast<double>(count) / wall_time_ms) : 0.0;
    const double render_loop_fps = render_ms > 0.0
        ? (1000.0 * static_cast<double>(count) / render_ms) : 0.0;
    const double render_only_fps = render_only_ms > 0.0
        ? (1000.0 * static_cast<double>(count) / render_only_ms) : 0.0;
    const double realtime_factor =
        (timings.target_fps && *timings.target_fps > 0 && end_to_end_fps > 0.0)
        ? (end_to_end_fps / static_cast<double>(*timings.target_fps)) : 0.0;

    const auto& first = frames.front();
    const auto* first_e = find_encoder_frame(enc, first.frame_number);
    const nlohmann::json first_frame{
        {"frame", first.frame_number},
        {"wall_duration_ms", first.duration_ms},
        {"queue_wait_ms", first.queue_wait_ms},
        {"render_total_ms", render_total_ms(first)},
        {"render", build_render_section(first)},
        {"conversion", build_conversion_section(is_native, first_e)},
        {"encoder", build_encoder_section(is_native, first_e)},
        {"image", build_image_section(first)},
        {"text", build_text_section(first)},
        {"cache", build_cache_section(first)}
    };

    nlohmann::json summary{
        {"first_frame_ms", stats.first_frame_ms},
        {"min_frame_ms", stats.min_frame_ms},
        {"max_frame_ms", stats.max_frame_ms},
        {"mean_frame_ms", stats.mean_frame_ms},
        {"avg_frame_ms", stats.mean_frame_ms},
        {"stddev_frame_ms", stats.stddev_frame_ms},
        {"p50_frame_ms", stats.p50_frame_ms},
        {"p90_frame_ms", stats.p90_frame_ms},
        {"p95_frame_ms", stats.p95_frame_ms},
        {"p99_frame_ms", stats.p99_frame_ms},
        {"warmup_frames", stats.warmup_frames},
        {"warmup_avg_ms", stats.warmup_frames > 0 ? nlohmann::json(stats.warmup_avg_ms) : nlohmann::json(nullptr)},
        {"steady_avg_ms", stats.steady_avg_ms},
        {"steady_p50_ms", stats.steady_p50_ms},
        {"steady_p95_ms", stats.steady_p95_ms},
        {"steady_p99_ms", stats.steady_p99_ms},
        {"render_only_fps", render_only_fps},
        {"render_loop_fps", render_loop_fps},
        {"end_to_end_fps", end_to_end_fps},
        {"measured_fps", end_to_end_fps},
        {"realtime_factor", realtime_factor},
        {"frame_budget_ms", frame_budget_ms},
        {"frames_over_budget", frames_over_budget},
        {"over_budget_ratio", over_budget_ratio}
    };
    uint64_t graph_reused_count = 0;
    uint64_t fast_path_reused_count = 0;
    for (const auto& f : frames) {
        if (f.graph_reused) ++graph_reused_count;
        if (f.fast_path_reused) ++fast_path_reused_count;
    }
    summary["graph_reused_frames"] = graph_reused_count;
    summary["graph_reused_ratio"] = count > 0
        ? static_cast<double>(graph_reused_count) / static_cast<double>(count) : 0.0;
    summary["fast_path_reused_frames"] = fast_path_reused_count;
    summary["first_frame"] = std::move(first_frame);
    if (timings.target_fps) {
        summary["target_fps"] = *timings.target_fps;
    } else {
        summary["target_fps"] = nullptr;
    }
    summary["target_fps_num"] = timings.target_fps_num
        ? nlohmann::json(*timings.target_fps_num) : nlohmann::json(nullptr);
    summary["target_fps_den"] = timings.target_fps_den
        ? nlohmann::json(*timings.target_fps_den) : nlohmann::json(nullptr);
    out["summary"] = std::move(summary);

    auto& job = out["job"];
    const auto put_ms = [&job](const char* key, const std::optional<double>& value) {
        if (value) job[key] = *value; else job[key] = nullptr;
    };
    put_ms("process_wall_ms", timings.process_wall_ms);
    if (timings.measurement_kind) job["measurement_kind"] = *timings.measurement_kind;
    else job["measurement_kind"] = nullptr;
    job["execution_path"] = timings.execution_path.value_or("unknown");
    job["surface_handoff_path"] = timings.surface_handoff_path.value_or("unknown");
    put_ms("job_wall_ms", timings.job_wall_ms);
    put_ms("engine_init_ms", timings.engine_init_ms);
    put_ms("backend_init_ms", timings.backend_init_ms);
    put_ms("plan_read_ms", timings.plan_read_ms);
    put_ms("plan_parse_ms", timings.plan_parse_ms);
    put_ms("plan_validate_ms", timings.plan_validate_ms);
    put_ms("plan_compile_ms", timings.plan_compile_ms);
    put_ms("graph_compile_ms", timings.graph_compile_ms);
    put_ms("prepare_ms", timings.prepare_ms);
    put_ms("render_loop_wall_ms", timings.render_loop_wall_ms);
    put_ms("encoder_finalize_ms", timings.encoder_finalize_ms);
    put_ms("mux_finalize_ms", timings.mux_finalize_ms);
    put_ms("output_finalize_ms", timings.output_finalize_ms);
    put_ms("validation_ms", timings.validation_ms);
    put_ms("ffprobe_ms", timings.ffprobe_ms);
    put_ms("sha256_ms", timings.sha256_ms);

    auto& enc_settings = job["encoder_settings"];
    if (timings.encoder_settings.preset) {
        enc_settings["preset"] = *timings.encoder_settings.preset;
    } else {
        enc_settings["preset"] = nullptr;
    }
    if (timings.encoder_settings.rate_control) {
        enc_settings["rate_control"] = *timings.encoder_settings.rate_control;
    } else {
        enc_settings["rate_control"] = nullptr;
    }
    if (timings.encoder_settings.async_depth) {
        enc_settings["async_depth"] = *timings.encoder_settings.async_depth;
    } else {
        enc_settings["async_depth"] = nullptr;
    }

    auto& gpu = job["gpu"];
    const auto put_gpu = [&gpu](const char* key, const std::optional<double>& value) {
        if (value) gpu[key] = *value; else gpu[key] = nullptr;
    };
    const auto put_gpu_u64 = [&gpu](const char* key, const std::optional<uint64_t>& value) {
        if (value) gpu[key] = *value; else gpu[key] = nullptr;
    };
    put_gpu("gpu_execute_ms", timings.gpu.gpu_execute_ms);
    put_gpu("gpu_readback_ms", timings.gpu.gpu_readback_ms);
    put_gpu("gpu_submit_cpu_ms", timings.gpu.gpu_submit_cpu_ms);
    put_gpu("gpu_wait_cpu_ms", timings.gpu.gpu_wait_cpu_ms);
    put_gpu_u64("standalone_wait_count", timings.gpu.standalone_wait_count);
    put_gpu_u64("standalone_wait_us", timings.gpu.standalone_wait_us);
    put_gpu_u64("frame_batch_drain_wait_count", timings.gpu.frame_batch_drain_wait_count);
    put_gpu_u64("frame_batch_drain_wait_us", timings.gpu.frame_batch_drain_wait_us);
    put_gpu_u64("frame_slot_wait_count", timings.gpu.frame_slot_wait_count);
    put_gpu_u64("frame_slot_wait_us", timings.gpu.frame_slot_wait_us);
    put_gpu_u64("gpu_readback_bytes", timings.gpu.gpu_readback_bytes);
    put_gpu_u64("gpu_upload_bytes", timings.gpu.gpu_upload_bytes);
    put_gpu_u64("gpu_upload_full_surface_bytes", timings.gpu.gpu_upload_full_surface_bytes);
    put_gpu_u64("gpu_upload_region_bytes", timings.gpu.gpu_upload_region_bytes);
    put_gpu_u64("native_surface_promotion_count", timings.gpu.native_surface_promotion_count);
    put_gpu_u64("native_surface_promotion_bytes", timings.gpu.native_surface_promotion_bytes);
    put_gpu_u64("native_surface_promotion_wall_us", timings.gpu.native_surface_promotion_wall_us);
    put_gpu_u64("native_surface_empty_create_count", timings.gpu.native_surface_empty_create_count);
    put_gpu_u64("native_surface_reuse_count", timings.gpu.native_surface_reuse_count);
    auto& upload_breakdown = gpu["upload_breakdown"];
    for (const auto& [key, value] : timings.gpu.upload_breakdown) {
        upload_breakdown[key] = value;
    }
    put_gpu_u64("gpu_submissions", timings.gpu.gpu_submissions);
    put_gpu_u64("passes_executed", timings.gpu.passes_executed);
    put_gpu_u64("gpu_nodes", timings.gpu.gpu_nodes);
    put_gpu_u64("software_fallback_nodes", timings.gpu.software_fallback_nodes);
    put_gpu_u64("gpu_native_surface_frames", timings.gpu.gpu_native_surface_frames);
    put_gpu_u64("gpu_native_encode_frames", timings.gpu.gpu_native_encode_frames);
    put_gpu_u64("nv12_to_rgba_frames", timings.gpu.nv12_to_rgba_frames);
    put_gpu_u64("rgba_to_nv12_frames", timings.gpu.rgba_to_nv12_frames);
    put_gpu_u64("gpu_surface_copy_frames", timings.gpu.gpu_surface_copy_frames);
    put_gpu_u64("cpu_pixel_readback_count", timings.gpu.cpu_pixel_readback_count);
    put_gpu_u64("cpu_pixel_readback_bytes", timings.gpu.cpu_pixel_readback_bytes);
    put_gpu_u64("video_pipe_fallback_frames", timings.gpu.video_pipe_fallback_frames);
    put_gpu_u64("video_native_fallback_frames", timings.gpu.video_native_fallback_frames);
    put_gpu_u64("gpu_surface_create_failures", timings.gpu.gpu_surface_create_failures);
    put_gpu_u64("gpu_encode_failures", timings.gpu.gpu_encode_failures);
    put_gpu_u64("frame_slot_wait_count", timings.gpu.frame_slot_wait_count);
    put_gpu_u64("frame_slot_wait_us", timings.gpu.frame_slot_wait_us);
    put_gpu_u64("cuda_vulkan_wait_count", timings.gpu.cuda_vulkan_wait_count);
    put_gpu_u64("cuda_vulkan_wait_submit_us", timings.gpu.cuda_vulkan_wait_submit_us);
    put_gpu_u64("cuda_vulkan_signal_count", timings.gpu.cuda_vulkan_signal_count);
    put_gpu_u64("cuda_vulkan_signal_submit_us", timings.gpu.cuda_vulkan_signal_submit_us);
    put_gpu_u64("cuda_composite_frames", timings.gpu.cuda_composite_frames);
    put_gpu_u64("cuda_composite_wall_us", timings.gpu.cuda_composite_wall_us);
    put_gpu_u64("cuda_encode_queue_peak", timings.gpu.cuda_encode_queue_peak);
    put_gpu_u64("cuda_encode_event_wait_count", timings.gpu.cuda_encode_event_wait_count);
    put_gpu_u64("cuda_encode_event_wait_us", timings.gpu.cuda_encode_event_wait_us);
    put_gpu_u64("encoder_staging_copy_bytes", timings.gpu.encoder_staging_copy_bytes);
    put_gpu_u64("video_decode_frames", timings.gpu.video_decode_frames);
    put_gpu_u64("video_decode_native_surface_frames", timings.gpu.video_decode_native_surface_frames);
    put_gpu_u64("video_decode_hw_transfer_frames", timings.gpu.video_decode_hw_transfer_frames);
    put_gpu_u64("video_decode_software_frames", timings.gpu.video_decode_software_frames);
    put_gpu_u64("video_decode_native_fallback_frames", timings.gpu.video_decode_native_fallback_frames);
    put_gpu_u64("video_prefetch_hits", timings.gpu.video_prefetch_hits);
    put_gpu_u64("video_prefetch_misses", timings.gpu.video_prefetch_misses);
    put_gpu_u64("video_prefetch_wait_us", timings.gpu.video_prefetch_wait_us);
    put_gpu_u64("video_prefetch_queue_clear_count", timings.gpu.video_prefetch_queue_clear_count);
    put_gpu_u64("video_prefetch_queue_depth_peak", timings.gpu.video_prefetch_queue_depth_peak);
    put_gpu_u64("video_decode_wall_ms", timings.gpu.video_decode_wall_ms);
    put_gpu_u64("video_decode_hw_transfer_wall_ms", timings.gpu.video_decode_hw_transfer_wall_ms);
    put_gpu_u64("video_decode_sws_wall_ms", timings.gpu.video_decode_sws_wall_ms);
    put_gpu_u64("video_decode_framebuffer_wall_ms", timings.gpu.video_decode_framebuffer_wall_ms);
    put_gpu_u64("hwframe_transfer_to_cpu_frames", timings.gpu.hwframe_transfer_to_cpu_frames);
    put_gpu_u64("software_color_convert_frames", timings.gpu.software_color_convert_frames);
    put_gpu_u64("cpu_full_surface_upload_bytes", timings.gpu.cpu_full_surface_upload_bytes);
    put_gpu_u64("gpu_readback_bytes", timings.gpu.gpu_readback_bytes);
    put_gpu_u64("nvenc_frames", timings.gpu.nvenc_frames);
    put_gpu_u64("software_encode_frames", timings.gpu.software_encode_frames);
    put_gpu_u64("bitstream_copy_frames", timings.gpu.bitstream_copy_frames);
    put_gpu_u64("vulkan_frames", timings.gpu.vulkan_frames);
    put_gpu_u64("cpu_readback_frames", timings.gpu.cpu_readback_frames);
    gpu["vulkan_frames"] = timings.gpu.vulkan_frames.value_or(uint64_t{0});
    gpu["cpu_readback_frames"] = timings.gpu.cpu_readback_frames.value_or(uint64_t{0});
    gpu["software_encode_frames"] = timings.gpu.software_encode_frames.value_or(uint64_t{0});
    gpu["nvenc_frames"] = timings.gpu.nvenc_frames.value_or(uint64_t{0});
    gpu["bitstream_copy_frames"] = timings.gpu.bitstream_copy_frames.value_or(uint64_t{0});
    put_gpu_u64("decode_submit_ms", timings.gpu.decode_submit_ms);
    put_gpu_u64("decode_wait_ms", timings.gpu.decode_wait_ms);
    put_gpu_u64("hwframe_transfer_ms", timings.gpu.hwframe_transfer_ms);
    put_gpu_u64("swscale_ms", timings.gpu.swscale_ms);
    put_gpu_u64("cpu_pixel_conversion_ms", timings.gpu.cpu_pixel_conversion_ms);
    put_gpu_u64("full_surface_upload_ms", timings.gpu.full_surface_upload_ms);
    put_gpu_u64("video_composite_ms", timings.gpu.video_composite_ms);
    put_gpu_u64("encode_submit_ms", timings.gpu.encode_submit_ms);
    put_gpu_u64("encode_wait_ms", timings.gpu.encode_wait_ms);
    std::string effective_backend = "unknown";
    if (timings.gpu.cuda_composite_frames && *timings.gpu.cuda_composite_frames > 0 &&
        (!timings.gpu.gpu_nodes || *timings.gpu.gpu_nodes == 0)) {
        effective_backend = "direct_yuv_cuda";
    } else if (timings.gpu.gpu_nodes && *timings.gpu.gpu_nodes > 0) {
        effective_backend = "vulkan";
    }
    gpu["effective_backend"] = effective_backend;

    std::string decoder_backend = "software";
    if (timings.gpu.video_decode_native_surface_frames && *timings.gpu.video_decode_native_surface_frames > 0) {
        if (!timings.gpu.video_decode_native_fallback_frames || *timings.gpu.video_decode_native_fallback_frames == 0) {
            decoder_backend = "nvdec";
        } else {
            decoder_backend = "hybrid";
        }
    }
    gpu["decoder_backend"] = decoder_backend;

    std::string encoder_backend = "software";
    if (timings.gpu.nvenc_frames && *timings.gpu.nvenc_frames > 0) {
        encoder_backend = "nvenc";
    }
    gpu["encoder_backend"] = encoder_backend;

    auto& prepare = job["prepare"];
    const auto put_prepare = [&prepare](const char* key, const std::optional<double>& value) {
        if (value) prepare[key] = *value; else prepare[key] = nullptr;
    };
    put_prepare("asset_preflight_ms", timings.prepare.asset_preflight_ms);
    put_prepare("asset_resolve_ms", timings.prepare.asset_resolve_ms);
    put_prepare("asset_decode_ms", timings.prepare.asset_decode_ms);
    put_prepare("font_resolve_ms", timings.prepare.font_resolve_ms);
    put_prepare("font_load_ms", timings.prepare.font_load_ms);
    put_prepare("text_shape_ms", timings.prepare.text_shape_ms);
    put_prepare("text_layout_ms", timings.prepare.text_layout_ms);
    put_prepare("glyph_raster_ms", timings.prepare.glyph_raster_ms);
    put_prepare("glyph_atlas_upload_ms", timings.prepare.glyph_atlas_upload_ms);
    put_prepare("plan_compile_ms", timings.prepare.plan_compile_ms);
    put_prepare("graph_compile_ms", timings.prepare.graph_compile_ms);
    put_prepare("resource_plan_ms", timings.prepare.resource_plan_ms);
    put_prepare("backend_prepare_ms", timings.prepare.backend_prepare_ms);

    auto& image = job["image"];
    const auto put_image = [&image](const char* key, const std::optional<double>& value) {
        if (value) image[key] = *value; else image[key] = nullptr;
    };
    put_image("resolve_ms", timings.prepare.image_resolve_ms);
    put_image("io_ms", timings.prepare.image_io_ms);
    put_image("decode_ms", timings.prepare.image_decode_ms);
    put_image("convert_ms", timings.prepare.image_convert_ms);
    put_image("upload_ms", timings.prepare.image_upload_ms);
    put_image("draw_ms", timings.image_draw_ms);
    const auto put_image_u64 = [&image](const char* key, const std::optional<uint64_t>& value) {
        if (value) image[key] = *value; else image[key] = nullptr;
    };
    put_image_u64("decode_count", timings.prepare.image_decode_count);
    put_image_u64("upload_count", timings.prepare.image_upload_count);
    put_image_u64("draw_count", timings.image_draw_count);

    auto& text = job["text"];
    const auto put_text = [&text](const char* key, const std::optional<double>& value) {
        if (value) text[key] = *value; else text[key] = nullptr;
    };
    put_text("font_resolve_ms", timings.text.font_resolve_ms);
    put_text("shaping_ms", timings.text.shaping_ms);
    put_text("bidi_ms", timings.text.bidi_ms);
    put_text("layout_ms", timings.text.layout_ms);
    put_text("glyph_cache_lookup_ms", timings.text.glyph_cache_lookup_ms);
    put_text("raster_ms", timings.text.raster_ms);
    put_text("atlas_upload_ms", timings.text.atlas_upload_ms);
    put_text("draw_ms", timings.text.draw_ms);
    const auto put_text_u64 = [&text](const char* key, const std::optional<uint64_t>& value) {
        if (value) text[key] = *value; else text[key] = nullptr;
    };
    put_text_u64("atlas_cache_hits", timings.text.atlas_cache_hits);
    put_text_u64("atlas_cache_misses", timings.text.atlas_cache_misses);
    put_text_u64("atlas_key_bytes_hashed", timings.text.atlas_key_bytes_hashed);
    put_text_u64("atlas_repack_count", timings.text.atlas_repack_count);
    put_text_u64("atlas_repack_bytes", timings.text.atlas_repack_bytes);
    put_text_u64("atlas_upload_count", timings.text.atlas_upload_count);
    put_text_u64("atlas_upload_bytes", timings.text.atlas_upload_bytes);
    put_text_u64("instance_upload_count", timings.text.instance_upload_count);
    put_text_u64("instance_upload_bytes", timings.text.instance_upload_bytes);

    auto& encoder = job["encoder"];
    const auto put_encoder = [&encoder](const char* key, const std::optional<double>& value) {
        if (value) encoder[key] = *value; else encoder[key] = nullptr;
    };
    const auto put_encoder_u64 = [&encoder](const char* key,
                                            const std::optional<uint64_t>& value) {
        if (value) encoder[key] = *value; else encoder[key] = nullptr;
    };
    put_encoder("submit_cpu_ms", timings.encoder.submit_cpu_ms);
    put_encoder("backpressure_wait_ms", timings.encoder.backpressure_wait_ms);
    put_encoder_u64("cuda_pending_peak", timings.encoder.cuda_pending_peak);
    put_encoder_u64("cuda_backpressure_wait_count", timings.encoder.cuda_backpressure_wait_count);
    put_encoder("flush_ms", timings.encoder.flush_ms);
    put_encoder("packet_receive_ms", timings.encoder.packet_receive_ms);
    put_encoder("mux_packet_ms", timings.encoder.mux_packet_ms);
    put_encoder("device_ms", timings.encoder.device_ms);
    put_encoder("pipe_write_cpu_ms", timings.encoder.pipe_write_cpu_ms);
    put_encoder("pipe_backpressure_wait_ms", timings.encoder.pipe_backpressure_wait_ms);
}

} // namespace chronon3d::cli::pipe_timing_detail
