#pragma once

#include <chronon3d/runtime/telemetry/frame_timing_summary.hpp>
#include <chronon3d/runtime/render_preparation.hpp>

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
    // Common backend upload accounting, keyed by gpu_upload_<producer>_<metric>.
    std::map<std::string, std::uint64_t> upload_breakdown;
    std::optional<std::uint64_t> gpu_submissions;
    std::optional<std::uint64_t> passes_executed;
    std::optional<std::uint64_t> gpu_nodes;
    std::optional<std::uint64_t> software_fallback_nodes;
    std::optional<std::uint64_t> software_fallback_us;
    std::optional<std::uint64_t> fallback_draw_node;
    std::optional<std::uint64_t> fallback_draw_image;
    std::optional<std::uint64_t> fallback_draw_other;
    std::optional<std::uint64_t> fallback_text_run;
    std::optional<std::uint64_t> fallback_composite;
    std::optional<std::uint64_t> fallback_composite_dimensions;
    std::optional<std::uint64_t> fallback_composite_mode;
    std::optional<std::uint64_t> fallback_effect;
    std::optional<std::uint64_t> fallback_blur;
    std::optional<std::uint64_t> fallback_dof;
    std::optional<std::uint64_t> gpu_native_surface_frames;
    std::optional<std::uint64_t> gpu_native_encode_frames;
    std::optional<std::uint64_t> gpu_surface_copy_frames;
    std::optional<std::uint64_t> cpu_pixel_readback_count;
    std::optional<std::uint64_t> cpu_pixel_readback_bytes;
    std::optional<std::uint64_t> video_pipe_fallback_frames;
    std::optional<std::uint64_t> video_native_fallback_frames;
    std::optional<std::uint64_t> gpu_surface_create_failures;
    std::optional<std::uint64_t> gpu_encode_failures;
    std::optional<std::uint64_t> interop_ring_wait_count;
    std::optional<std::uint64_t> interop_ring_wait_us;
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
};

struct JobTimings {
    std::optional<double> process_wall_ms;
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
    GpuMetrics gpu;
    chronon3d::runtime::RenderPreparationTimings prepare;
    std::optional<int> target_fps;
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
