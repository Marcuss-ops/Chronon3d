#include "pipe_timing_sidecar.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace chronon3d::cli {
using pipe_timing::JobTimings;

void write_frame_timing_sidecar(
    const std::string& video_path,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& render_frames,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& encoder_frames,
    double wall_time_ms,
    double render_ms,
    double encode_ms,
    const JobTimings& timings,
    bool is_native)
{
    if (render_frames.empty()) return;

    std::vector<chronon3d::telemetry::FrameTelemetry> frames = render_frames;
    std::vector<chronon3d::telemetry::FrameTelemetry> enc = encoder_frames;
    std::sort(frames.begin(), frames.end(), [](const auto& a, const auto& b) {
        return a.frame_number < b.frame_number;
    });
    std::sort(enc.begin(), enc.end(), [](const auto& a, const auto& b) {
        return a.frame_number < b.frame_number;
    });

    nlohmann::json out{
        {"schema", "chronon3d.frame-timing.v1"},
        {"video", video_path},
        {"wall_time_ms", wall_time_ms},
        {"render_ms", render_ms},
        {"encode_close_ms", encode_ms},
        {"frames_total", frames.size()},
        {"time_source", "steady_clock"},
        {"frame_times_ms", nlohmann::json::array()}
    };

    std::vector<double> durations;
    durations.reserve(frames.size());

    const auto render_total = [](const chronon3d::telemetry::FrameTelemetry& f) {
        return f.direct_yuv_decode_ms > 0.0 ? f.direct_yuv_decode_ms : f.graph_eval_ms;
    };
    const auto build_render_section = [&render_total](const chronon3d::telemetry::FrameTelemetry& f) {
        return nlohmann::json{
            {"timeline_eval_ms", f.render_breakdown.timeline_eval_ms},
            {"animation_eval_ms", nullptr},
            {"text_ms", f.render_breakdown.text_ms},
            {"graph_prepare_ms", f.render_breakdown.graph_prepare_ms},
            {"graph_execute_ms", f.render_breakdown.graph_execute_ms},
            {"compositing_ms", f.render_breakdown.compositing_ms},
            {"effects_ms", f.render_breakdown.effects_ms},
            {"surface_management_ms", f.render_breakdown.surface_management_ms},
            {"backend_overhead_ms", f.render_breakdown.backend_overhead_ms},
            {"accounted_cpu_ms", f.render_breakdown.accounted_cpu_ms},
            {"unaccounted_cpu_ms", f.render_breakdown.unaccounted_cpu_ms},
            {"direct_yuv_decode_ms", f.direct_yuv_decode_ms > 0.0 ? nlohmann::json(f.direct_yuv_decode_ms) : nlohmann::json(nullptr)},
            {"total_ms", render_total(f)}
        };
    };
    const auto build_conversion_section = [is_native](const chronon3d::telemetry::FrameTelemetry* e) {
        nlohmann::json section{
            {"pixel_format_convert_ms", e ? e->pixel_format_convert_ms : 0.0},
            {"color_space_convert_ms", e ? e->color_space_convert_ms : 0.0},
            {"scale_ms", nullptr},
            {"cpu_copy_ms", nullptr},
            {"gpu_readback_ms", nullptr},
            {"encoder_buffer_copy_ms", nullptr},
            {"convert_ms", nullptr},
            {"copy_ms", nullptr},
            {"total_ms", e ? e->conversion_copy_ms : 0.0}
        };
        if (is_native && e) {
            section["convert_ms"] = e->native_convert_ms;
        }
        return section;
    };
    const auto build_encoder_section = [is_native](const chronon3d::telemetry::FrameTelemetry* e) {
        nlohmann::json section{
            {"submit_cpu_ms", nullptr},
            {"backpressure_wait_ms", nullptr},
            {"pipe_write_cpu_ms", nullptr},
            {"pipe_backpressure_wait_ms", nullptr},
            {"flush_ms", nullptr},
            {"packet_receive_ms", nullptr},
            {"mux_packet_ms", nullptr},
            {"device_ms", nullptr}
        };
        if (e) {
            if (is_native) {
                section["submit_cpu_ms"] = e->encoder_ms;
                section["backpressure_wait_ms"] = e->backpressure_wait_ms;
            } else {
                section["pipe_write_cpu_ms"] = e->pipe_write_cpu_ms;
                section["pipe_backpressure_wait_ms"] = e->pipe_backpressure_wait_ms;
            }
        }
        return section;
    };
    const auto build_image_section = [](const chronon3d::telemetry::FrameTelemetry& f) {
        return nlohmann::json{
            {"resolve_ms", nullptr},
            {"io_ms", nullptr},
            {"decode_ms", nullptr},
            {"convert_ms", nullptr},
            {"upload_ms", nullptr},
            {"draw_ms", f.image_timing.draw_ms},
            {"draw_count", f.image_timing.draw_count}
        };
    };
    const auto build_text_section = [](const chronon3d::telemetry::FrameTelemetry& f) {
        return nlohmann::json{
            {"font_resolve_ms", nullptr},
            {"shaping_ms", f.text_timing.shaping_ms},
            {"bidi_ms", f.text_timing.bidi_ms},
            {"layout_ms", f.text_timing.layout_ms},
            {"glyph_cache_lookup_ms", f.text_timing.glyph_cache_lookup_ms},
            {"raster_ms", f.text_timing.raster_ms},
            {"atlas_upload_ms", f.text_timing.atlas_upload_ms},
            {"draw_ms", f.text_timing.draw_ms}
        };
    };
    const auto build_cache_section = [](const chronon3d::telemetry::FrameTelemetry& f) {
        return nlohmann::json{
            {"node_lookup_ms", f.node_lookup_ms},
            {"node_hit", f.cache_hit}
        };
    };
    const auto find_encoder = [&enc](int frame_number) -> const chronon3d::telemetry::FrameTelemetry* {
        const auto it = std::lower_bound(enc.begin(), enc.end(), frame_number,
            [](const chronon3d::telemetry::FrameTelemetry& f, int n) { return f.frame_number < n; });
        return (it != enc.end() && it->frame_number == frame_number) ? &*it : nullptr;
    };

    std::size_t ei = 0;
    for (const auto& frame : frames) {
        while (ei < enc.size() && enc[ei].frame_number < frame.frame_number) ++ei;
        const auto* e = (ei < enc.size() && enc[ei].frame_number == frame.frame_number)
            ? &enc[ei] : nullptr;
        durations.push_back(frame.duration_ms);

        out["frame_times_ms"].push_back({
            {"frame", frame.frame_number},
            {"wall_start_ms", frame.wall_start_ms},
            {"wall_end_ms", frame.wall_start_ms + frame.duration_ms},
            {"wall_duration_ms", frame.duration_ms},
            {"queue_wait_ms", frame.queue_wait_ms},
            {"render", build_render_section(frame)},
            {"conversion", build_conversion_section(e)},
            {"encoder", build_encoder_section(e)},
            {"image", build_image_section(frame)},
            {"text", build_text_section(frame)},
            {"cache", build_cache_section(frame)},
            {"render_ms", render_total(frame)},
            {"end_to_end_render_thread_ms", frame.duration_ms},
            {"conversion_copy_ms", e ? e->conversion_copy_ms : 0.0},
            {"encoder_ms", e ? e->encoder_ms : 0.0},
            {"pipe_write_ms", e ? e->pipe_write_ms : 0.0},
            {"native_convert_ms", e ? e->native_convert_ms : 0.0},
            {"native_send_ms", e ? e->native_send_ms : 0.0},
            {"native_receive_ms", e ? e->native_receive_ms : 0.0},
            {"native_mux_ms", e ? e->native_mux_ms : 0.0},
            {"node_lookup_ms", frame.node_lookup_ms},
            {"cache_hit", frame.cache_hit},
            {"dirty_area_ratio", frame.dirty_area_ratio},
            {"fast_path_reused", frame.fast_path_reused},
            {"graph_reused", frame.graph_reused}
        });
    }

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
    for (const auto& f : frames) render_only_ms += render_total(f);
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
    const auto* first_e = find_encoder(first.frame_number);
    const nlohmann::json first_frame{
        {"frame", first.frame_number},
        {"wall_duration_ms", first.duration_ms},
        {"queue_wait_ms", first.queue_wait_ms},
        {"render_total_ms", render_total(first)},
        {"render", build_render_section(first)},
        {"conversion", build_conversion_section(first_e)},
        {"encoder", build_encoder_section(first_e)},
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

    auto& cache = out["cache"];
    if (timings.cache.node_lookup_ms) cache["node_lookup_ms"] = *timings.cache.node_lookup_ms;
    else cache["node_lookup_ms"] = nullptr;
    const auto put_cache_u64 = [&cache](const char* key, const std::optional<uint64_t>& value) {
        if (value) cache[key] = *value; else cache[key] = nullptr;
    };
    put_cache_u64("node_cache_hits", timings.cache.node_cache_hits);
    put_cache_u64("node_cache_misses", timings.cache.node_cache_misses);
    put_cache_u64("image_cache_hits", timings.cache.image_cache_hits);
    put_cache_u64("image_cache_misses", timings.cache.image_cache_misses);
    put_cache_u64("font_cache_hits", timings.cache.font_cache_hits);
    put_cache_u64("font_cache_misses", timings.cache.font_cache_misses);
    put_cache_u64("glyph_cache_hits", timings.cache.glyph_cache_hits);
    put_cache_u64("glyph_cache_misses", timings.cache.glyph_cache_misses);
    put_cache_u64("gpu_asset_cache_hits", timings.cache.gpu_asset_cache_hits);
    put_cache_u64("gpu_asset_cache_misses", timings.cache.gpu_asset_cache_misses);

    const uint64_t glyph_hits = timings.cache.glyph_cache_hits.value_or(uint64_t{0});
    const uint64_t glyph_misses = timings.cache.glyph_cache_misses.value_or(uint64_t{0});
    cache["glyph_hit_ratio"] = (glyph_hits + glyph_misses > 0)
        ? (static_cast<double>(glyph_hits) / static_cast<double>(glyph_hits + glyph_misses))
        : 0.0;

    auto& runtime = job["runtime"];
    const auto put_rt_u64 = [&runtime](const char* key, const std::optional<uint64_t>& value) {
        if (value) runtime[key] = *value; else runtime[key] = nullptr;
    };
    const auto put_rt_f = [&runtime](const char* key, const std::optional<double>& value) {
        if (value) runtime[key] = *value; else runtime[key] = nullptr;
    };
    put_rt_u64("video_runtime_created", timings.runtime.video_runtime_created);
    put_rt_u64("video_runtime_reused", timings.runtime.video_runtime_reused);
    put_rt_u64("cuda_hwdevice_created", timings.runtime.cuda_hwdevice_created);
    put_rt_u64("cuda_hwdevice_reused", timings.runtime.cuda_hwdevice_reused);
    put_rt_u64("cuda_frames_cache_hit", timings.runtime.cuda_frames_cache_hit);
    put_rt_u64("cuda_frames_cache_miss", timings.runtime.cuda_frames_cache_miss);
    put_rt_u64("cuda_image_cache_hit", timings.runtime.cuda_image_cache_hit);
    put_rt_u64("cuda_image_cache_miss", timings.runtime.cuda_image_cache_miss);
    put_rt_f("encoder_open_nvenc_ms", timings.runtime.encoder_open_nvenc_ms);
    const uint64_t rt_created = timings.runtime.video_runtime_created.value_or(uint64_t{0});
    const uint64_t rt_reused = timings.runtime.video_runtime_reused.value_or(uint64_t{0});
    runtime["runtime_reuse_ratio"] = (rt_created + rt_reused > 0)
        ? (static_cast<double>(rt_reused) / static_cast<double>(rt_created + rt_reused))
        : 0.0;

    auto& cpu = job["cpu_breakdown"];
    const auto put_cpu = [&cpu](const char* key, const std::optional<double>& value) {
        if (value) cpu[key] = *value; else cpu[key] = nullptr;
    };
    const auto put_cpu_u64 = [&cpu](const char* key, const std::optional<uint64_t>& value) {
        if (value) cpu[key] = *value; else cpu[key] = nullptr;
    };
    put_cpu("timeline_eval_ms", timings.cpu_breakdown.timeline_eval_ms);
    put_cpu("graph_resolve_layers_ms", timings.cpu_breakdown.graph_resolve_layers_ms);
    put_cpu("graph_dirty_rect_ms", timings.cpu_breakdown.graph_dirty_rect_ms);
    put_cpu("graph_build_ms", timings.cpu_breakdown.graph_build_ms);
    put_cpu("graph_execute_ms", timings.cpu_breakdown.graph_execute_ms);
    put_cpu("compiled_graph_refresh_ms", timings.cpu_breakdown.compiled_graph_refresh_ms);
    put_cpu("cache_eval_ms", timings.cpu_breakdown.cache_eval_ms);
    put_cpu("dirty_eval_ms", timings.cpu_breakdown.dirty_eval_ms);
    put_cpu("input_resolve_ms", timings.cpu_breakdown.input_resolve_ms);
    put_cpu("predicted_bbox_ms", timings.cpu_breakdown.predicted_bbox_ms);
    put_cpu("clone_context_ms", timings.cpu_breakdown.clone_context_ms);
    put_cpu("state_assign_ms", timings.cpu_breakdown.state_assign_ms);
    put_cpu("framebuffer_acquire_ms", timings.cpu_breakdown.framebuffer_acquire_ms);
    put_cpu("framebuffer_clear_ms", timings.cpu_breakdown.framebuffer_clear_ms);
    put_cpu("framebuffer_lifetime_ms", timings.cpu_breakdown.framebuffer_lifetime_ms);
    put_cpu("node_schedule_ms", timings.cpu_breakdown.node_schedule_ms);
    put_cpu("node_dispatch_ms", timings.cpu_breakdown.node_dispatch_ms);
    put_cpu("node_execute_actual_ms", timings.cpu_breakdown.node_execute_actual_ms);
    put_cpu("node_overhead_ms", timings.cpu_breakdown.node_overhead_ms);
    put_cpu("telemetry_emit_ms", timings.cpu_breakdown.telemetry_emit_ms);
    put_cpu("text_layout_ms", timings.cpu_breakdown.text_layout_ms);
    put_cpu("text_rasterization_ms", timings.cpu_breakdown.text_rasterization_ms);
    put_cpu("text_shaping_ms", timings.cpu_breakdown.text_shaping_ms);
    put_cpu("text_bidi_ms", timings.cpu_breakdown.text_bidi_ms);
    put_cpu("glyph_cache_lookup_ms", timings.cpu_breakdown.glyph_cache_lookup_ms);
    put_cpu("glyph_atlas_upload_ms", timings.cpu_breakdown.glyph_atlas_upload_ms);
    put_cpu("text_draw_ms", timings.cpu_breakdown.text_draw_ms);
    put_cpu("clearnode_ms", timings.cpu_breakdown.clearnode_ms);
    put_cpu("compositenode_blend_ms", timings.cpu_breakdown.compositenode_blend_ms);
    put_cpu("effect_stack_total_ms", timings.cpu_breakdown.effect_stack_total_ms);
    put_cpu_u64("graph_executed_frames", timings.cpu_breakdown.graph_executed_frames);
    put_cpu_u64("graph_reused_frames", timings.cpu_breakdown.graph_reused_frames);
    put_cpu_u64("fast_path_reused_frames", timings.cpu_breakdown.fast_path_reused_frames);
    put_cpu_u64("video_source_requested_frames", timings.cpu_breakdown.video_source_requested_frames);
    put_cpu_u64("video_source_inactive_frames", timings.cpu_breakdown.video_source_inactive_frames);
    put_cpu_u64("video_source_repeated_frames", timings.cpu_breakdown.video_source_repeated_frames);

    auto& hardware = job["hardware"];
    const auto put_hardware = [&hardware](const char* key, const std::optional<double>& value) {
        if (value) hardware[key] = *value; else hardware[key] = nullptr;
    };
    const auto put_hardware_u64 = [&hardware](const char* key, const std::optional<uint64_t>& value) {
        if (value) hardware[key] = *value; else hardware[key] = nullptr;
    };
    put_hardware("gpu_utilization_avg", timings.hardware.gpu_utilization_avg);
    put_hardware("gpu_utilization_peak", timings.hardware.gpu_utilization_peak);
    put_hardware("nvdec_utilization_avg", timings.hardware.nvdec_utilization_avg);
    put_hardware("nvdec_utilization_peak", timings.hardware.nvdec_utilization_peak);
    put_hardware("nvenc_utilization_avg", timings.hardware.nvenc_utilization_avg);
    put_hardware("nvenc_utilization_peak", timings.hardware.nvenc_utilization_peak);
    put_hardware("memory_utilization_avg", timings.hardware.memory_utilization_avg);
    put_hardware_u64("vram_used_peak_mb", timings.hardware.vram_used_peak_mb);
    put_hardware_u64("vram_total_mb", timings.hardware.vram_total_mb);

    auto& memory = out["memory"];
    if (timings.framebuffer_allocations) {
        memory["framebuffer_allocations"] = *timings.framebuffer_allocations;
        memory["framebuffer_allocations_per_frame"] = count > 0
            ? (static_cast<double>(*timings.framebuffer_allocations) / static_cast<double>(count))
            : 0.0;
    } else {
        memory["framebuffer_allocations"] = nullptr;
        memory["framebuffer_allocations_per_frame"] = nullptr;
    }
    memory["framebuffer_alloc_text"] = timings.framebuffer_alloc_text.value_or(0);
    memory["framebuffer_alloc_effect"] = timings.framebuffer_alloc_effect.value_or(0);
    memory["framebuffer_alloc_glow"] = timings.framebuffer_alloc_glow.value_or(0);
    memory["framebuffer_alloc_video"] = timings.framebuffer_alloc_video.value_or(0);
    memory["framebuffer_alloc_graph"] = timings.framebuffer_alloc_graph.value_or(0);
    memory["framebuffer_alloc_scratch"] = timings.framebuffer_alloc_scratch.value_or(0);
    memory["framebuffer_alloc_unknown"] = timings.framebuffer_alloc_unknown.value_or(0);

    auto& wall_tl = out["exclusive_wall_timeline"];
    const auto put_wt = [&wall_tl](const char* key, const std::optional<double>& value) {
        if (value) wall_tl[key] = *value; else wall_tl[key] = nullptr;
    };
    put_wt("process_wall_ms", timings.exclusive_wall.process_wall_ms);
    put_wt("startup_ms", timings.exclusive_wall.startup_ms);
    put_wt("input_open_ms", timings.exclusive_wall.input_open_ms);
    put_wt("prepare_ms", timings.exclusive_wall.prepare_ms);
    put_wt("render_loop_ms", timings.exclusive_wall.render_loop_ms);
    put_wt("encoder_drain_finalize_ms", timings.exclusive_wall.encoder_drain_finalize_ms);
    put_wt("mux_finalize_ms", timings.exclusive_wall.mux_finalize_ms);
    put_wt("validation_ms", timings.exclusive_wall.validation_ms);
    put_wt("ffprobe_ms", timings.exclusive_wall.ffprobe_ms);
    put_wt("sha256_ms", timings.exclusive_wall.sha256_ms);
    put_wt("output_finalize_ms", timings.exclusive_wall.output_finalize_ms);
    put_wt("sidecar_report_ms", timings.exclusive_wall.sidecar_report_ms);
    put_wt("unaccounted_ms", timings.exclusive_wall.unaccounted_ms);
    put_wt("accounted_percent", timings.exclusive_wall.accounted_percent);

    auto& internal = out["internal_profiling"];
    auto& dec = internal["decode"];
    if (timings.internal_decode.decoded_frames) dec["decoded_frames"] = *timings.internal_decode.decoded_frames; else dec["decoded_frames"] = nullptr;
    if (timings.internal_decode.decode_total_ms) dec["decode_total_ms"] = *timings.internal_decode.decode_total_ms; else dec["decode_total_ms"] = nullptr;
    if (timings.internal_decode.demux_read_packet_ms) dec["demux_read_packet_ms"] = *timings.internal_decode.demux_read_packet_ms; else dec["demux_read_packet_ms"] = nullptr;
    if (timings.internal_decode.avcodec_send_packet_ms) dec["avcodec_send_packet_ms"] = *timings.internal_decode.avcodec_send_packet_ms; else dec["avcodec_send_packet_ms"] = nullptr;
    if (timings.internal_decode.avcodec_receive_frame_ms) dec["avcodec_receive_frame_ms"] = *timings.internal_decode.avcodec_receive_frame_ms; else dec["avcodec_receive_frame_ms"] = nullptr;
    if (timings.internal_decode.nvdec_wait_ms) dec["nvdec_wait_ms"] = *timings.internal_decode.nvdec_wait_ms; else dec["nvdec_wait_ms"] = nullptr;
    if (timings.internal_decode.cpu_active_ms) dec["cpu_active_ms"] = *timings.internal_decode.cpu_active_ms; else dec["cpu_active_ms"] = nullptr;
    if (timings.internal_decode.cpu_wait_ms) dec["cpu_wait_ms"] = *timings.internal_decode.cpu_wait_ms; else dec["cpu_wait_ms"] = nullptr;
    if (timings.internal_decode.avg_ms_per_frame) dec["avg_ms_per_frame"] = *timings.internal_decode.avg_ms_per_frame; else dec["avg_ms_per_frame"] = nullptr;
    if (timings.internal_decode.p50_ms_per_frame) dec["p50_ms_per_frame"] = *timings.internal_decode.p50_ms_per_frame; else dec["p50_ms_per_frame"] = nullptr;
    if (timings.internal_decode.p95_ms_per_frame) dec["p95_ms_per_frame"] = *timings.internal_decode.p95_ms_per_frame; else dec["p95_ms_per_frame"] = nullptr;
    if (timings.internal_decode.max_ms_per_frame) dec["max_ms_per_frame"] = *timings.internal_decode.max_ms_per_frame; else dec["max_ms_per_frame"] = nullptr;

    auto& dyuv = internal["direct_yuv"];
    if (timings.internal_direct_yuv.input_probe_ms) dyuv["input_probe_ms"] = *timings.internal_direct_yuv.input_probe_ms; else dyuv["input_probe_ms"] = nullptr;
    if (timings.internal_direct_yuv.scene_eval_ms) dyuv["scene_eval_ms"] = *timings.internal_direct_yuv.scene_eval_ms; else dyuv["scene_eval_ms"] = nullptr;
    if (timings.internal_direct_yuv.watermark_image_load_ms) dyuv["watermark_image_load_ms"] = *timings.internal_direct_yuv.watermark_image_load_ms; else dyuv["watermark_image_load_ms"] = nullptr;
    if (timings.internal_direct_yuv.watermark_cuda_upload_ms) dyuv["watermark_cuda_upload_ms"] = *timings.internal_direct_yuv.watermark_cuda_upload_ms; else dyuv["watermark_cuda_upload_ms"] = nullptr;
    if (timings.internal_direct_yuv.prepare_update_ms) dyuv["prepare_update_ms"] = *timings.internal_direct_yuv.prepare_update_ms; else dyuv["prepare_update_ms"] = nullptr;
    if (timings.internal_direct_yuv.cuda_launch_ms) dyuv["cuda_launch_ms"] = *timings.internal_direct_yuv.cuda_launch_ms; else dyuv["cuda_launch_ms"] = nullptr;
    if (timings.internal_direct_yuv.cuda_event_wait_ms) dyuv["cuda_event_wait_ms"] = *timings.internal_direct_yuv.cuda_event_wait_ms; else dyuv["cuda_event_wait_ms"] = nullptr;
    if (timings.internal_direct_yuv.cuda_kernel_total_ms) dyuv["cuda_kernel_total_ms"] = *timings.internal_direct_yuv.cuda_kernel_total_ms; else dyuv["cuda_kernel_total_ms"] = nullptr;

    auto& enc_p = internal["encoder"];
    if (timings.internal_encoder.av_hwframe_get_buffer_ms) enc_p["av_hwframe_get_buffer_ms"] = *timings.internal_encoder.av_hwframe_get_buffer_ms; else enc_p["av_hwframe_get_buffer_ms"] = nullptr;
    if (timings.internal_encoder.surface_acquire_ms) enc_p["surface_acquire_ms"] = *timings.internal_encoder.surface_acquire_ms; else enc_p["surface_acquire_ms"] = nullptr;
    if (timings.internal_encoder.nvenc_submit_ms) enc_p["nvenc_submit_ms"] = *timings.internal_encoder.nvenc_submit_ms; else enc_p["nvenc_submit_ms"] = nullptr;
    if (timings.internal_encoder.queue_backpressure_wait_ms) enc_p["queue_backpressure_wait_ms"] = *timings.internal_encoder.queue_backpressure_wait_ms; else enc_p["queue_backpressure_wait_ms"] = nullptr;
    if (timings.internal_encoder.packet_drain_ms) enc_p["packet_drain_ms"] = *timings.internal_encoder.packet_drain_ms; else enc_p["packet_drain_ms"] = nullptr;
    if (timings.internal_encoder.cpu_active_ms) enc_p["cpu_active_ms"] = *timings.internal_encoder.cpu_active_ms; else enc_p["cpu_active_ms"] = nullptr;
    if (timings.internal_encoder.cpu_wait_ms) enc_p["cpu_wait_ms"] = *timings.internal_encoder.cpu_wait_ms; else enc_p["cpu_wait_ms"] = nullptr;

    auto& sb = out["startup_breakdown"];
    sb["cli_init_ms"] = timings.startup_breakdown.cli_init_ms;
    sb["logger_init_ms"] = timings.startup_breakdown.logger_init_ms;
    sb["cli_bootstrap_ms"] = timings.startup_breakdown.cli_bootstrap_ms;
    sb["cli_parse_ms"] = timings.startup_breakdown.cli_parse_ms;
    sb["composition_registration_ms"] = timings.startup_breakdown.composition_registration_ms;
    sb["plan_read_ms"] = timings.startup_breakdown.plan_read_ms;
    sb["plan_json_parse_ms"] = timings.startup_breakdown.plan_json_parse_ms;
    sb["plan_decode_validate_ms"] = timings.startup_breakdown.plan_decode_validate_ms;
    sb["plan_asset_resolve_ms"] = timings.startup_breakdown.plan_asset_resolve_ms;
    sb["plan_compile_ms"] = timings.startup_breakdown.plan_compile_ms;
    sb["plan_prepare_ms"] = timings.startup_breakdown.plan_prepare_ms;
    sb["encoder_create_ms"] = timings.startup_breakdown.encoder_create_ms;
    sb["encoder_open_hw_ctx_ms"] = timings.startup_breakdown.encoder_open_hw_ctx_ms;
    sb["cuda_compositor_warmup_ms"] = timings.startup_breakdown.cuda_compositor_warmup_ms;
    sb["encoder_open_nvenc_ms"] = timings.startup_breakdown.encoder_open_nvenc_ms;
    sb["encoder_open_mux_header_ms"] = timings.startup_breakdown.encoder_open_mux_header_ms;
    sb["vulkan_instance_ms"] = timings.startup_breakdown.vulkan_instance_ms;
    sb["vulkan_device_ms"] = timings.startup_breakdown.vulkan_device_ms;
    sb["vulkan_pipelines_ms"] = timings.startup_breakdown.vulkan_pipelines_ms;
    sb["renderer_runtime_init_ms"] = timings.startup_breakdown.renderer_runtime_init_ms;
    sb["other_startup_ms"] = timings.startup_breakdown.other_startup_ms;
    sb["total_startup_ms"] = timings.startup_breakdown.total_startup_ms;
    sb["accounted_ms"] = timings.startup_breakdown.accounted_ms;
    sb["unaccounted_ms"] = timings.startup_breakdown.unaccounted_ms;
    sb["phases_observed"] = timings.startup_breakdown.phases_observed;

    auto& pb = out["prepare_breakdown"];
    pb["font_preflight_ms"] = timings.prepare_breakdown.font_preflight_ms;
    pb["pool_warmup_ms"] = timings.prepare_breakdown.pool_warmup_ms;
    pb["triple_arena_alloc_ms"] = timings.prepare_breakdown.triple_arena_alloc_ms;
    pb["writer_thread_spawn_ms"] = timings.prepare_breakdown.writer_thread_spawn_ms;
    pb["other_prepare_ms"] = timings.prepare_breakdown.other_prepare_ms;
    pb["total_prepare_ms"] = timings.prepare_breakdown.total_prepare_ms;

    const auto sidecar = std::filesystem::path(video_path).string() + ".timing.json";
    std::ofstream file(sidecar);
    if (!file) {
        spdlog::warn("[video] Could not write frame timing sidecar: {}", sidecar);
        return;
    }
    file << out.dump(2) << '\n';
    spdlog::info("[video] Wrote exact per-frame timing sidecar: {}", sidecar);
}

} // namespace chronon3d::cli
