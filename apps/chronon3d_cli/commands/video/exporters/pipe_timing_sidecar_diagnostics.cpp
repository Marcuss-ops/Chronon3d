// ═══════════════════════════════════════════════════════════════════════════
// pipe_timing_sidecar_diagnostics.cpp — phase 3 of the frame-timing sidecar.
//
// Split out of pipe_timing_sidecar.cpp (pure code move, no schema change):
// cache / memory / exclusive-wall-timeline / internal-profiling / startup /
// prepare-breakdown sections.
// ═══════════════════════════════════════════════════════════════════════════

#include "pipe_timing_sidecar_detail.hpp"

#include <cstdint>

namespace chronon3d::cli::pipe_timing_detail {

void build_diagnostics_sections(
    nlohmann::json& out, const pipe_timing::JobTimings& timings, std::size_t frame_count) {
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

    auto& runtime = out["job"]["runtime"];
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

    auto& cpu = out["job"]["cpu_breakdown"];
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

    auto& hardware = out["job"]["hardware"];
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
        memory["framebuffer_allocations_per_frame"] = frame_count > 0
            ? (static_cast<double>(*timings.framebuffer_allocations) / static_cast<double>(frame_count))
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
}

} // namespace chronon3d::cli::pipe_timing_detail
