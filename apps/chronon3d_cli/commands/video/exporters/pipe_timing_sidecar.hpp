#pragma once

#include <chronon3d/runtime/telemetry/frame_timing_summary.hpp>
#include <chronon3d/runtime/render_preparation.hpp>
#include "../common/pipe_startup_breakdown.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::cli::pipe_timing {

struct CacheMetrics {
    std::optional<double> node_lookup_ms;
    std::optional<std::uint64_t> node_cache_hits;
    std::optional<std::uint64_t> node_cache_misses;
    std::optional<std::uint64_t> image_cache_hits;
    std::optional<std::uint64_t> image_cache_misses;
    std::optional<std::uint64_t> font_cache_hits;
    std::optional<std::uint64_t> font_cache_misses;
    std::optional<std::uint64_t> glyph_cache_hits;
    std::optional<std::uint64_t> glyph_cache_misses;
    std::optional<std::uint64_t> gpu_asset_cache_hits;
    std::optional<std::uint64_t> gpu_asset_cache_misses;
};

/// Process-persistent GPU runtime reuse telemetry. Proves that the
/// VideoRuntimeRegistry + VideoDeviceRuntime stay alive across clips
/// (runtime_reused > 0) instead of being recreated per clip
/// (runtime_created > 0 is the churn we want to minimize).
struct RuntimeMetrics {
    std::optional<std::uint64_t> video_runtime_created;
    std::optional<std::uint64_t> video_runtime_reused;
    std::optional<std::uint64_t> cuda_hwdevice_created;
    std::optional<std::uint64_t> cuda_hwdevice_reused;
    std::optional<std::uint64_t> cuda_frames_cache_hit;
    std::optional<std::uint64_t> cuda_frames_cache_miss;
    std::optional<std::uint64_t> cuda_image_cache_hit;
    std::optional<std::uint64_t> cuda_image_cache_miss;
    std::optional<double> encoder_open_nvenc_ms;
};

struct TextMetrics {
    std::optional<double> font_resolve_ms;
    std::optional<double> shaping_ms;
    std::optional<double> bidi_ms;
    std::optional<double> layout_ms;
    std::optional<double> glyph_cache_lookup_ms;
    std::optional<double> raster_ms;
    std::optional<double> atlas_upload_ms;
    std::optional<double> draw_ms;
    std::optional<std::uint64_t> atlas_cache_hits;
    std::optional<std::uint64_t> atlas_cache_misses;
    std::optional<std::uint64_t> atlas_key_bytes_hashed;
    std::optional<std::uint64_t> atlas_repack_count;
    std::optional<std::uint64_t> atlas_repack_bytes;
    std::optional<std::uint64_t> atlas_upload_count;
    std::optional<std::uint64_t> atlas_upload_bytes;
    std::optional<std::uint64_t> instance_upload_count;
    std::optional<std::uint64_t> instance_upload_bytes;
};

/// Encoder settings for this job. The `preset`/`rate_control`/`async_depth`
/// values are what was RESOLVED and applied at open() time (after engine
/// defaults are filled in); the `requested_*` fields keep what the caller
/// explicitly asked for ("engine-default" when never requested). Telemetry
/// therefore reveals requested vs resolved configuration instead of
/// pretending an engine default was an explicit user choice. Empty optionals
/// = not a native encoder job.
struct AppliedEncoderSettings {
    std::optional<std::string> preset;
    std::optional<std::string> rate_control;
    std::optional<int> async_depth;
    std::optional<std::string> requested_rate_control;
    std::optional<std::string> requested_preset;
};

struct EncoderMetrics {
    std::optional<double> submit_cpu_ms;
    std::optional<double> backpressure_wait_ms;
    std::optional<std::uint64_t> cuda_pending_peak;
    std::optional<std::uint64_t> cuda_backpressure_wait_count;
    std::optional<double> flush_ms;
    std::optional<double> packet_receive_ms;
    std::optional<double> mux_packet_ms;
    std::optional<double> device_ms;
    std::optional<double> pipe_write_cpu_ms;
    std::optional<double> pipe_backpressure_wait_ms;
};

struct GpuMetrics {
    std::optional<double> gpu_execute_ms;
    std::optional<double> gpu_readback_ms;
    std::optional<double> gpu_submit_cpu_ms;
    std::optional<double> gpu_wait_cpu_ms;
    std::optional<std::uint64_t> standalone_wait_count;
    std::optional<std::uint64_t> standalone_wait_us;
    std::optional<std::uint64_t> frame_batch_drain_wait_count;
    std::optional<std::uint64_t> frame_batch_drain_wait_us;
    std::optional<std::uint64_t> frame_slot_wait_count;
    std::optional<std::uint64_t> frame_slot_wait_us;
    std::optional<std::uint64_t> gpu_readback_bytes;
    std::optional<std::uint64_t> gpu_upload_bytes;
    std::optional<std::uint64_t> gpu_upload_full_surface_bytes;
    std::optional<std::uint64_t> gpu_upload_region_bytes;
    std::optional<std::uint64_t> native_surface_promotion_count;
    std::optional<std::uint64_t> native_surface_promotion_bytes;
    std::optional<std::uint64_t> native_surface_promotion_wall_us;
    std::optional<std::uint64_t> native_surface_empty_create_count;
    std::optional<std::uint64_t> native_surface_reuse_count;
    // Common backend upload accounting, keyed by gpu_upload_<producer>_<metric>.
    std::map<std::string, std::uint64_t> upload_breakdown;
    std::optional<std::uint64_t> gpu_submissions;
    std::optional<std::uint64_t> passes_executed;
    std::optional<std::uint64_t> gpu_nodes;
    std::optional<std::uint64_t> software_fallback_nodes;
    std::optional<std::uint64_t> gpu_native_surface_frames;
    std::optional<std::uint64_t> gpu_native_encode_frames;
    std::optional<std::uint64_t> nv12_to_rgba_frames;
    std::optional<std::uint64_t> rgba_to_nv12_frames;
    std::optional<std::uint64_t> gpu_surface_copy_frames;
    std::optional<std::uint64_t> cpu_pixel_readback_count;
    std::optional<std::uint64_t> cpu_pixel_readback_bytes;
    std::optional<std::uint64_t> video_pipe_fallback_frames;
    std::optional<std::uint64_t> video_native_fallback_frames;
    std::optional<std::uint64_t> gpu_surface_create_failures;
    std::optional<std::uint64_t> gpu_encode_failures;
    std::optional<std::uint64_t> cuda_vulkan_wait_count;
    std::optional<std::uint64_t> cuda_vulkan_wait_submit_us;
    std::optional<std::uint64_t> cuda_vulkan_signal_count;
    std::optional<std::uint64_t> cuda_vulkan_signal_submit_us;
    std::optional<std::uint64_t> cuda_composite_frames;
    std::optional<std::uint64_t> cuda_composite_wall_us;
    std::optional<std::uint64_t> cuda_encode_queue_peak;
    std::optional<std::uint64_t> cuda_encode_event_wait_count;
    std::optional<std::uint64_t> cuda_encode_event_wait_us;
    std::optional<std::uint64_t> encoder_staging_copy_bytes;
    std::optional<std::uint64_t> video_decode_frames;
    std::optional<std::uint64_t> video_decode_native_surface_frames;
    std::optional<std::uint64_t> video_decode_hw_transfer_frames;
    std::optional<std::uint64_t> video_decode_software_frames;
    std::optional<std::uint64_t> video_decode_native_fallback_frames;
    std::optional<std::uint64_t> video_prefetch_hits;
    std::optional<std::uint64_t> video_prefetch_misses;
    std::optional<std::uint64_t> video_prefetch_wait_us;
    std::optional<std::uint64_t> video_prefetch_queue_clear_count;
    std::optional<std::uint64_t> video_prefetch_queue_depth_peak;
    std::optional<std::uint64_t> video_decode_wall_ms;
    std::optional<std::uint64_t> video_decode_hw_transfer_wall_ms;
    std::optional<std::uint64_t> video_decode_sws_wall_ms;
    std::optional<std::uint64_t> video_decode_framebuffer_wall_ms;
    std::optional<std::uint64_t> hwframe_transfer_to_cpu_frames;
    std::optional<std::uint64_t> software_color_convert_frames;
    std::optional<std::uint64_t> cpu_full_surface_upload_bytes;
    std::optional<std::uint64_t> nvenc_frames;
    std::optional<std::uint64_t> software_encode_frames;
    std::optional<std::uint64_t> bitstream_copy_frames;
    std::optional<std::uint64_t> vulkan_frames;
    std::optional<std::uint64_t> cpu_readback_frames;
    std::optional<std::uint64_t> decode_submit_ms;
    std::optional<std::uint64_t> decode_wait_ms;
    std::optional<std::uint64_t> hwframe_transfer_ms;
    std::optional<std::uint64_t> swscale_ms;
    std::optional<std::uint64_t> cpu_pixel_conversion_ms;
    std::optional<std::uint64_t> full_surface_upload_ms;
    std::optional<std::uint64_t> video_composite_ms;
    std::optional<std::uint64_t> encode_submit_ms;
    std::optional<std::uint64_t> encode_wait_ms;
};

struct CpuBreakdownMetrics {
    std::optional<double> timeline_eval_ms;
    std::optional<double> graph_resolve_layers_ms;
    std::optional<double> graph_dirty_rect_ms;
    std::optional<double> graph_build_ms;
    std::optional<double> graph_execute_ms;
    std::optional<double> compiled_graph_refresh_ms;
    std::optional<double> cache_eval_ms;
    std::optional<double> dirty_eval_ms;
    std::optional<double> input_resolve_ms;
    std::optional<double> predicted_bbox_ms;
    std::optional<double> clone_context_ms;
    std::optional<double> state_assign_ms;
    std::optional<double> framebuffer_acquire_ms;
    std::optional<double> framebuffer_clear_ms;
    std::optional<double> framebuffer_lifetime_ms;
    std::optional<double> node_schedule_ms;
    std::optional<double> node_dispatch_ms;
    std::optional<double> node_execute_actual_ms;
    std::optional<double> node_overhead_ms;
    std::optional<double> telemetry_emit_ms;
    std::optional<double> text_layout_ms;
    std::optional<double> text_rasterization_ms;
    std::optional<double> text_shaping_ms;
    std::optional<double> text_bidi_ms;
    std::optional<double> glyph_cache_lookup_ms;
    std::optional<double> glyph_atlas_upload_ms;
    std::optional<double> text_draw_ms;
    std::optional<double> clearnode_ms;
    std::optional<double> compositenode_blend_ms;
    std::optional<double> effect_stack_total_ms;
    std::optional<std::uint64_t> graph_executed_frames;
    std::optional<std::uint64_t> graph_reused_frames;
    std::optional<std::uint64_t> fast_path_reused_frames;
    std::optional<std::uint64_t> video_source_requested_frames;
    std::optional<std::uint64_t> video_source_inactive_frames;
    std::optional<std::uint64_t> video_source_repeated_frames;
};

struct HardwareMetrics {
    std::optional<double> gpu_utilization_avg;
    std::optional<double> gpu_utilization_peak;
    std::optional<double> nvdec_utilization_avg;
    std::optional<double> nvdec_utilization_peak;
    std::optional<double> nvenc_utilization_avg;
    std::optional<double> nvenc_utilization_peak;
    std::optional<double> memory_utilization_avg;
    std::optional<std::uint64_t> vram_used_peak_mb;
    std::optional<std::uint64_t> vram_total_mb;
};

struct ExclusiveWallTimeline {
    std::optional<double> process_wall_ms;
    std::optional<double> startup_ms;
    std::optional<double> input_open_ms;
    std::optional<double> prepare_ms;
    std::optional<double> render_loop_ms;
    std::optional<double> encoder_drain_finalize_ms;
    std::optional<double> mux_finalize_ms;
    std::optional<double> validation_ms;
    std::optional<double> ffprobe_ms;
    std::optional<double> sha256_ms;
    std::optional<double> output_finalize_ms;
    std::optional<double> sidecar_report_ms;
    std::optional<double> unaccounted_ms;
    std::optional<double> accounted_percent;
};

struct InternalDecodeProfiling {
    std::optional<uint64_t> decoded_frames;
    std::optional<double> decode_total_ms;
    std::optional<double> demux_read_packet_ms;
    std::optional<double> avcodec_send_packet_ms;
    std::optional<double> avcodec_receive_frame_ms;
    std::optional<double> nvdec_wait_ms;
    std::optional<double> cpu_active_ms;
    std::optional<double> cpu_wait_ms;
    std::optional<double> avg_ms_per_frame;
    std::optional<double> p50_ms_per_frame;
    std::optional<double> p95_ms_per_frame;
    std::optional<double> max_ms_per_frame;
};

struct InternalDirectYuvProfiling {
    std::optional<double> input_probe_ms;
    std::optional<double> scene_eval_ms;
    std::optional<double> watermark_image_load_ms;
    std::optional<double> watermark_cuda_upload_ms;
    std::optional<double> prepare_update_ms;
    std::optional<double> cuda_launch_ms;
    std::optional<double> cuda_event_wait_ms;
    std::optional<double> cuda_kernel_total_ms;
};

struct InternalEncoderProfiling {
    std::optional<double> av_hwframe_get_buffer_ms;
    std::optional<double> surface_acquire_ms;
    std::optional<double> nvenc_submit_ms;
    std::optional<double> queue_backpressure_wait_ms;
    std::optional<double> packet_drain_ms;
    std::optional<double> cpu_active_ms;
    std::optional<double> cpu_wait_ms;
};

struct JobTimings {
    std::optional<double> process_wall_ms;
    std::optional<std::string> measurement_kind;
    // Canonical execution path string: "direct_yuv" | "full_graph" |
    // "bitstream_copy" | "smart_gop_copy". Determined by the exporter from
    // the session's direct_yuv_selected() + encoder_backend, NOT from
    // counter heuristics. This is the single authoritative field; the
    // gpu.effective_backend field is kept as a compatibility projection.
    std::optional<std::string> execution_path;
    std::optional<std::string> surface_handoff_path;
    std::optional<double> job_wall_ms;
    std::optional<double> engine_init_ms;
    std::optional<double> backend_init_ms;
    std::optional<double> plan_read_ms;
    std::optional<double> plan_parse_ms;
    std::optional<double> plan_validate_ms;
    std::optional<double> plan_compile_ms;
    std::optional<double> graph_compile_ms;
    std::optional<double> prepare_ms;
    std::optional<double> render_loop_wall_ms;
    std::optional<double> encoder_finalize_ms;
    std::optional<double> mux_finalize_ms;
    std::optional<double> output_finalize_ms;
    std::optional<double> validation_ms;
    std::optional<double> ffprobe_ms;
    std::optional<double> sha256_ms;
    std::optional<std::uint64_t> framebuffer_allocations;
    std::optional<std::uint64_t> framebuffer_alloc_text;
    std::optional<std::uint64_t> framebuffer_alloc_effect;
    std::optional<std::uint64_t> framebuffer_alloc_glow;
    std::optional<std::uint64_t> framebuffer_alloc_video;
    std::optional<std::uint64_t> framebuffer_alloc_graph;
    std::optional<std::uint64_t> framebuffer_alloc_scratch;
    std::optional<std::uint64_t> framebuffer_alloc_unknown;
    std::optional<double> image_draw_ms;
    std::optional<std::uint64_t> image_draw_count;
    CacheMetrics cache;
    TextMetrics text;
    EncoderMetrics encoder;
    AppliedEncoderSettings encoder_settings;
    GpuMetrics gpu;
    RuntimeMetrics runtime;
    CpuBreakdownMetrics cpu_breakdown;
    HardwareMetrics hardware;
    chronon3d::runtime::RenderPreparationTimings prepare;
    std::optional<double> target_fps;
    std::optional<int> target_fps_num;
    std::optional<int> target_fps_den;
    ExclusiveWallTimeline exclusive_wall;
    InternalDecodeProfiling internal_decode;
    InternalDirectYuvProfiling internal_direct_yuv;
    InternalEncoderProfiling internal_encoder;
    StartupBreakdown startup_breakdown;
    PrepareBreakdown prepare_breakdown;
    std::optional<double> startup_accounted_ms;
    std::optional<double> startup_unaccounted_ms;
};

} // namespace chronon3d::cli::pipe_timing

namespace chronon3d::cli {
void write_frame_timing_sidecar(
    const std::string& video_path,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& render_frames,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& encoder_frames,
    double wall_time_ms,
    double render_ms,
    double encode_ms,
    const pipe_timing::JobTimings& timings,
    bool is_native);
} // namespace chronon3d::cli
