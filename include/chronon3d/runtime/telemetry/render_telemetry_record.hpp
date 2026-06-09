#pragma once
#include <chronon3d/core/dirty_fallback_reason.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace chronon3d::telemetry {

struct RenderTelemetryRecord {
    std::string run_id;
    std::string composition_id;
    std::string output_path;

    bool success{false};
    std::string error_code;
    std::string error_message;

    int frames_total{0};
    int frames_written{0};

    double wall_time_ms{0.0};
    double render_ms{0.0};
    double encode_ms{0.0};
    double effective_fps{0.0};

    // System-wide performance counters mapped from RenderCounters
    uint64_t pixels_touched{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
    uint64_t nodes_executed{0};
    uint64_t layers_rendered{0};
    uint64_t text_glyphs_rasterized{0};
    uint64_t images_sampled{0};
    uint64_t blur_pixels{0};
    uint64_t simd_lerp_calls{0};

    uint64_t bytes_allocated_peak{0};
    uint64_t node_cache_hash_collisions{0};

    uint64_t clear_skipped_calls{0};
    uint64_t clear_skipped_pixels{0};
    uint64_t clear_calls{0};
    uint64_t clear_pixels{0};
    uint64_t clearnode_copy_pixels{0};
    uint64_t composite_copy_pixels{0};
    uint64_t clearnode_bytes_avoided{0};
    uint64_t clearnode_memcpy_bytes{0};
    uint64_t clearnode_memcpy_calls{0};
    uint64_t clearnode_detach_shared_count{0};
    uint64_t clearnode_partial_clip_copy_count{0};
    uint64_t clearnode_full_clip_skip_count{0};
    uint64_t prev_fb_use_count_sum{0};
    uint64_t prev_fb_use_count_samples{0};
    uint64_t prev_fb_use_count_peak{0};
    uint64_t composite_calls{0};
    uint64_t composite_pixels{0};
    uint64_t transform_calls{0};
    uint64_t transform_pixels{0};
    uint64_t effect_stack_calls{0};
    uint64_t effect_pixels{0};
    uint64_t layer_culling_tests{0};
    uint64_t layers_culled{0};
    uint64_t layers_visible{0};
    uint64_t framebuffer_allocations{0};
    uint64_t framebuffer_reuses{0};
    uint64_t framebuffer_bytes_allocated{0};
    uint64_t framebuffer_bytes_peak{0};
    uint64_t dirty_rect_count{0};
    uint64_t dirty_pixels{0};
    uint64_t dirty_union_area_pixels{0};
    uint64_t dirty_full_fallbacks{0};
    uint64_t bypass_not_cacheable_count{0};
    uint64_t dirty_full_fallback_predicted_bounds_missing{0};
    uint64_t dirty_full_fallback_composite_missing_input_bounds{0};
    uint64_t dirty_full_fallback_transform_bounds_unknown{0};
    uint64_t dirty_full_fallback_effect_bounds_unknown{0};

    uint64_t framebuffer_acquire_ms{0};
    uint64_t framebuffer_clear_ms{0};
    uint64_t clearnode_ms{0};
    uint64_t clearnode_restore_ms{0};
    uint64_t framebuffer_pool_clear_ms{0};
    uint64_t framebuffer_enqueue_ms{0};
    uint64_t framebuffer_pool_miss_count_size_mismatch{0};
    uint64_t framebuffer_pool_miss_count_empty{0};
    uint64_t framebuffer_pool_miss_count_best_fit{0};
    uint64_t framebuffer_pool_hits{0};
    uint64_t framebuffer_buffer_returned_to_pool_count{0};
    uint64_t unaligned_memory_copies{0};
    uint64_t frame_conversion_copy_ms{0};
    uint64_t video_graph_eval_ms{0};
    uint64_t video_conversion_ms{0};
    uint64_t video_pipe_write_ms{0};
    uint64_t video_ffmpeg_latency_ms{0};
    uint64_t io_queue_push_blocked_ms{0};
    uint64_t io_queue_pop_wait_ms{0};
    uint64_t io_writer_idle_wait_ms{0};
    uint64_t io_queue_peak_depth{0};
    uint64_t ffmpeg_pipe_write_blocked_ms{0};
    uint64_t converted_frame_cache_hits{0};
    uint64_t ffmpeg_flush_ms{0};
    uint64_t io_queue_peak_bytes{0};

    // ── Setup Deep Dive (cold start diagnostics) ──
    uint64_t setup_graph_parsing_ms{0};
    uint64_t setup_asset_io_load_ms{0};
    uint64_t setup_pool_preallocation_ms{0};
    double image_decode_ms{0.0};

    // ── Graph Executor Phase Timings ──
    uint64_t compiled_graph_refresh_ms{0};
    uint64_t cache_eval_ms{0};
    uint64_t dirty_eval_ms{0};
    uint64_t input_resolve_ms{0};
    uint64_t predicted_bbox_ms{0};
    uint64_t clone_context_ms{0};
    uint64_t state_assign_ms{0};
    uint64_t framebuffer_lifetime_ms{0};
    uint64_t node_schedule_ms{0};
    uint64_t node_dispatch_ms{0};
    uint64_t node_execute_actual_ms{0};
    uint64_t node_overhead_ms{0};
    uint64_t telemetry_emit_ms{0};

    // ── Chronon Render Throughput Benchmark (pure Chronon pipeline) ──
    double chronon_render_only_ms{0.0};     // graph + cache + pixel ops (excl. conversion/copy/queue)
    double chronon_conversion_copy_ms{0.0};   // pixel conversion + copy for encoder
    double chronon_queue_wait_ms{0.0};        // time render thread blocked on full queue
    double chronon_render_throughput_ms{0.0}; // chronon_render_only + conversion_copy + queue_wait

    // ── End-to-End Export Benchmark (incl. FFmpeg) ──
    double ffmpeg_encode_total_ms{0.0};   // FFmpeg pipe write blocked (codec + pipe I/O)
    double ffmpeg_flush_close_ms{0.0};    // FFmpeg flush + mux + file close after render loop
    double e2e_wall_ms{0.0};              // total wall time (setup + render + encode + close)

    // ── Cache efficiency derived metrics ──
    double cache_hit_rate{0.0};
    double dirty_area_ratio{0.0};
    double framebuffer_reuse_rate{0.0};

    // ── Diagnostic / Generic Counters (queried from render_counters table) ──
    uint64_t tiles_total{0};
    uint64_t tiles_hit{0};
    uint64_t tiles_miss{0};
    uint64_t tiles_partial{0};

    uint64_t process_context_switches_voluntary{0};
    uint64_t process_context_switches_involuntary{0};
    uint64_t os_page_faults_major{0};
    uint64_t os_page_faults_minor{0};
    uint64_t ffmpeg_cpu_user_pct{0};
    uint64_t ffmpeg_cpu_sys_pct{0};
    uint64_t llc_references{0};
    uint64_t llc_misses{0};
    double image_sample_ms{0.0};
    uint64_t image_sampled_pixels{0};

    // Host & environment specs
    std::string started_at_iso;
    std::string finished_at_iso;
    std::string git_commit_short;
    std::string build_type;
    std::string compiler_info;
    std::string os;
    std::string cpu_model;
    int cores{0};
};

struct FrameTelemetryRecord {
    int frame_number{0};
    double duration_ms{0.0};
    bool cache_hit{false};
    double dirty_area_ratio{1.0};
    double graph_eval_ms{0.0};
    double queue_wait_ms{0.0};
    double conversion_copy_ms{0.0};
    double encoder_ms{0.0};
    double pipe_write_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};

    // ── Per-frame dirty-rect state (populated by render loop) ──
    bool dirty_rect_enabled{false};
    int dirty_rect_x0{0};
    int dirty_rect_y0{0};
    int dirty_rect_x1{0};
    int dirty_rect_y1{0};
    bool tile_execution_used{false};
    bool fast_path_reused{false};
    bool graph_reused{false};
};

struct FrameEncoderTelemetryRecord {
    int frame_number{0};
    double conversion_copy_ms{0.0};
    double encoder_ms{0.0};
    double pipe_write_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};
};

struct PhaseTelemetryRecord {
    std::string phase_name;
    double duration_ms{0.0};
};

struct CounterTelemetryRecord {
    std::string counter_name;
    uint64_t counter_value{0};
};

// ── Per-node telemetry (populated during GraphExecutor::execute_node) ──────────
struct NodeTelemetryRecord {
    std::string run_id;
    int frame_number{0};
    std::string node_name;
    std::string node_type;       // stringified RenderGraphNodeKind
    std::string layer_id;        // layer name when known, empty otherwise
    double duration_ms{0.0};
    std::string cache_status;    // "hit", "miss", "bypass_no_cache", "bypass_not_cacheable"
    std::string cache_key_digest;
    int input_count{0};
    int output_width{0};
    int output_height{0};
    uint64_t output_bytes{0};
    float bbox_x{0}, bbox_y{0}, bbox_w{0}, bbox_h{0};
    float visible_x{0}, visible_y{0}, visible_w{0}, visible_h{0};
    uint64_t pixels_touched{0};
    uint64_t pixels_cleared{0};
    uint64_t pixels_composited{0};
    uint64_t pixels_transformed{0};
    uint64_t pixels_blurred{0};
};

// ── Per-layer telemetry (aggregated from scene/layer pipeline) ─────────────────
struct LayerTelemetryRecord {
    std::string run_id;
    int frame_number{0};
    std::string layer_id;
    std::string layer_name;
    std::string layer_type;      // stringified LayerKind
    double duration_ms{0.0};
    bool visible{true};
    std::string cull_reason;     // "" if visible, descriptive reason if culled
    float opacity{1.0f};
    std::string blend_mode{"Normal"};
    float bbox_x{0}, bbox_y{0}, bbox_w{0}, bbox_h{0};
    float visible_x{0}, visible_y{0}, visible_w{0}, visible_h{0};
    int area_pixels{0};
    int visible_pixels{0};
    int dirty_pixels{0};
    std::string effects;         // comma-separated effect names
    float effect_padding{0};
    int glyphs_rasterized{0};
    int images_sampled{0};
};

struct CacheTelemetryRecord {
    std::string run_id;
    int frame_number{0};
    std::string node_name;
    bool cacheable{false};
    std::string cache_status; // "hit", "miss_non_cacheable", "miss_hash_mismatch", etc.
    uint64_t key_digest{0};
    uint64_t params_hash{0};
    uint64_t source_hash{0};
    uint64_t input_hash{0};
    uint64_t output_bytes{0};

    // ── Individual NodeCacheKey components for diagnosing digest changes ──
    int key_width{0};
    int key_height{0};
    int key_frame{0};          // NodeCacheKey.frame (may differ from animation frame)
    int key_tile_x{0};
    int key_tile_y{0};
    int key_tile_size{0};
    uint64_t key_tile_hash{0};
};

struct CullingTelemetryRecord {
    std::string run_id;
    int frame_number{0};
    std::string layer_id;
    bool visible{true};
    std::string reason;
    float bbox_x{0}, bbox_y{0}, bbox_w{0}, bbox_h{0};
    float visible_x{0}, visible_y{0}, visible_w{0}, visible_h{0};
    uint64_t saved_pixels{0};
};

struct TextTelemetryRecord {
    std::string run_id;
    int frame_number{0};
    std::string layer_id;
    int text_length{0};
    int line_count{0};
    int glyph_count{0};
    int glyphs_rasterized{0};
    int glyph_cache_hits{0};
    int glyph_cache_misses{0};
    double layout_ms{0.0};
    double raster_ms{0.0};
    double composite_ms{0.0};
    std::string font_path;
    double font_size{0.0};
};

struct ImageTelemetryRecord {
    std::string run_id;
    int frame_number{0};
    std::string layer_id;
    std::string image_path;
    int image_width{0};
    int image_height{0};
    std::string cache_status; // "hit", "miss_decode", etc.
    double decode_ms{0.0};
    double sample_ms{0.0};
    uint64_t sampled_pixels{0};
};

struct TileTelemetryRecord {
    std::string run_id;
    int frame_number{0};
    std::string layer_id;
    int tile_x{0};
    int tile_y{0};
    std::string tile_status; // "hit", "miss", "partial"
    int dirty_rects_count{0};
};

} // namespace chronon3d::telemetry
