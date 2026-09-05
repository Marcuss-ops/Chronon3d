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

    uint64_t framebuffer_acquire_wall_ms{0};
    uint64_t framebuffer_clear_wall_ms{0};
    uint64_t clearnode_wall_ms{0};
    uint64_t clearnode_restore_wall_ms{0};
    uint64_t clearnode_restore_rect_count{0};
    uint64_t clearnode_restore_pixels{0};
    uint64_t clearnode_restore_bytes{0};
    uint64_t clearnode_restore_full_frame_count{0};
    uint64_t clearnode_restore_dirty_rect_count{0};
    uint64_t clearnode_restore_noop_count{0};
    uint64_t framebuffer_pool_clear_wall_ms{0};
    uint64_t framebuffer_enqueue_wall_ms{0};
    uint64_t framebuffer_pool_empty_alloc{0};
    uint64_t framebuffer_pool_best_fit_reuse{0};
    uint64_t framebuffer_pool_exact_hit{0};
    uint64_t framebuffer_buffer_returned_to_pool_count{0};
    uint64_t framebuffer_pool_budget_bytes{0};
    uint64_t framebuffer_pool_retained_bytes{0};
    uint64_t framebuffer_pool_evicted_count{0};
    uint64_t framebuffer_pool_evicted_bytes{0};
    uint64_t framebuffer_pool_pressure_count{0};
    uint64_t framebuffer_pool_size_class_count{0};
    uint64_t logical_resource_count{0};
    uint64_t physical_resource_slot_count{0};
    uint64_t logical_resource_bytes{0};
    uint64_t physical_resource_bytes{0};
    uint64_t alias_saved_bytes{0};
    uint64_t alias_reuse_count{0};
    uint64_t new_resource_slot_count{0};
    uint64_t arena_peak_bytes{0};
    uint64_t unaligned_memory_copies{0};
    uint64_t frame_conversion_copy_wall_ms{0};
    uint64_t video_graph_eval_wall_ms{0};
    uint64_t video_conversion_wall_ms{0};
    uint64_t video_pipe_write_wall_ms{0};
    uint64_t video_ffmpeg_wait_ms{0};
    uint64_t io_queue_push_wait_ms{0};
    uint64_t io_queue_pop_wait_ms{0};
    uint64_t io_writer_idle_wait_ms{0};
    uint64_t io_queue_peak_depth{0};
    uint64_t ffmpeg_pipe_write_wall_ms{0};
    uint64_t converted_frame_cache_hits{0};
    uint64_t program_cache_hits{0};
    uint64_t program_cache_misses{0};
    uint64_t program_cache_evictions{0};
    uint64_t ffmpeg_flush_wall_ms{0};
    uint64_t io_queue_peak_bytes{0};

    // ── Setup Deep Dive (cold start diagnostics) ──
    uint64_t setup_graph_parsing_wall_ms{0};
    uint64_t setup_asset_io_load_wall_ms{0};
    uint64_t setup_pool_preallocation_wall_ms{0};
    double image_decode_wall_ms{0.0};

    // ── Graph Executor Phase Timings ──
    uint64_t compiled_graph_refresh_wall_ms{0};
    uint64_t cache_eval_wall_ms{0};
    uint64_t dirty_eval_wall_ms{0};
    uint64_t input_resolve_wall_ms{0};
    uint64_t predicted_bbox_wall_ms{0};
    uint64_t clone_context_wall_ms{0};
    uint64_t state_assign_wall_ms{0};
    uint64_t framebuffer_lifetime_wall_ms{0};
    uint64_t node_schedule_wall_ms{0};
    uint64_t node_dispatch_wall_ms{0};
    uint64_t node_execute_actual_wall_ms{0};
    uint64_t node_overhead_wall_ms{0};
    uint64_t telemetry_emit_wall_ms{0};

    // ── Chronon Render Throughput Benchmark (pure Chronon pipeline) ──
    double chronon_render_only_ms{0.0};     // graph + cache + pixel ops (excl. conversion/copy/queue)
    double chronon_conversion_copy_ms{0.0};   // pixel conversion + copy for encoder
    double chronon_queue_wait_ms{0.0};        // time render thread blocked on full queue
    double chronon_render_throughput_ms{0.0}; // chronon_render_only + conversion_copy + queue_wait

    // ── End-to-End Export Benchmark (incl. FFmpeg) ──
    double ffmpeg_encode_total_ms{0.0};   // FFmpeg pipe write blocked (codec + pipe I/O)
    double ffmpeg_flush_close_ms{0.0};    // FFmpeg flush + mux + file close after render loop
    double e2e_wall_ms{0.0};              // total wall time (setup + render + encode + close)

    // ── Canonical per-phase render pipeline breakdown ──
    // Five non-overlapping phases (mirrors RenderPhaseTimings) surfaced in the
    // telemetry summary so render speed is never conflated with encode/IO.
    double phase_scene_eval_ms{0.0};   // graph/scene eval, easing, layout, scheduling
    double phase_gpu_render_ms{0.0};   // node pixel execution (transform/composite/blur)
    double phase_gpu_readback_ms{0.0}; // framebuffer → encoder conversion/copy
    double phase_encode_ms{0.0};       // codec encode of the readback frames
    double phase_disk_io_ms{0.0};      // pipe/mux + flush/close writes to disk

    // ── Output verification & startup cost ──
    // Measured on the video-export path; 0.0 on still/frame renders where no
    // output contract verification runs. Never estimated from wall time.
    double ffprobe_wall_ms{0.0};   // ffprobe subprocess during output verification
    double sha256_wall_ms{0.0};    // SHA-256 digest during output verification
    double process_startup_ms{0.0};// process start → job start wall time
    // Framebuffer allocation events per rendered frame (post-warmup). This is
    // the only per-frame allocation event rate the engine measures; there is
    // no general heap-allocator counter, so it is never estimated.
    double framebuffer_allocations_per_frame{0.0};

    // ── GPU overlay-factory counters (queried from render_counters) ──
    // Fed by the GPU backend when it records telemetry; 0 on software runs.
    // `gpu_submissions` is the vkQueueSubmit count (1 per command batch);
    // `passes_executed` is the number of GPU command-plan passes executed.
    uint64_t gpu_submissions{0};
    uint64_t passes_executed{0};
    uint64_t gpu_submit_cpu_us{0};
    uint64_t gpu_wait_cpu_us{0};
    uint64_t gpu_readback_bytes{0};
    uint64_t gpu_upload_bytes{0};
    uint64_t physical_surfaces_peak{0};
    uint64_t barrier_count{0};
    double gpu_execute_ms{0.0};
    double gpu_readback_ms{0.0};
    double gpu_submit_cpu_ms{0.0};
    double gpu_wait_cpu_ms{0.0};

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

/// Per-frame image sampling timing (the draw/composite phase of the image
/// asset pipeline).  Resolve/io/decode/convert happen once in the prepare
/// barrier (see RenderPreparationTimings), so only the draw phase is
/// per-frame measurable here.
struct FrameImageTiming {
    double draw_ms{0.0};
    uint64_t draw_count{0};
};

/// Per-frame text pipeline timing, in ms.  Populated by the render loop
/// from per-frame counter deltas.  A field stays 0.0 when that phase did
/// not run this frame (e.g. shaping/layout are prepare-only and steady
/// state ≈ 0).  font_resolve is prepare-only, so it is reported at job
/// level (RenderPreparationTimings / job.text), not per-frame.
struct FrameTextTiming {
    double shaping_ms{0.0};
    double bidi_ms{0.0};
    double layout_ms{0.0};
    double glyph_cache_lookup_ms{0.0};
    double raster_ms{0.0};
    double atlas_upload_ms{0.0};
    double draw_ms{0.0};
};

/// Architectural breakdown of a single frame's render time (render_ms), in ms.
/// Populated by the render loop from per-frame counter deltas.  A field stays
/// 0.0 when that phase is not yet measured (emitted as JSON null, never 0.0).
struct FrameRenderBreakdown {
    double timeline_eval_ms{0.0};
    double animation_eval_ms{0.0};
    double text_ms{0.0};
    double graph_prepare_ms{0.0};
    double graph_execute_ms{0.0};
    double compositing_ms{0.0};
    double effects_ms{0.0};
    double surface_management_ms{0.0};
    double backend_overhead_ms{0.0};
    double accounted_cpu_ms{0.0};
    double unaccounted_cpu_ms{0.0};
};

// The single canonical per-frame record.  Formerly split across
// FrameTelemetryRecord + FrameEncoderTelemetryRecord (which duplicated the
// encoder/conversion fields); the two were merged so one frame == one record
// and the render thread + encoder thread write disjoint fields of the same
// slot.
struct FrameTelemetry {
    int frame_number{0};
    // Monotonic wall-clock offset from the beginning of the render loop.
    // This is intentionally a measured timestamp, not a presentation-time
    // estimate derived from FPS.
    double wall_start_ms{0.0};
    double duration_ms{0.0};
    bool cache_hit{false};
    double dirty_area_ratio{1.0};
    double node_lookup_ms{0.0};
    double graph_eval_ms{0.0};
    // Direct-YUV execution time. This is deliberately separate from
    // graph_eval_ms because the DirectCudaYuvProgram bypasses RenderGraph.
    double direct_yuv_decode_ms{0.0};
    double queue_wait_ms{0.0};
    FrameRenderBreakdown render_breakdown{};
    FrameImageTiming image_timing{};
    FrameTextTiming text_timing{};
    double conversion_copy_ms{0.0};
    double pixel_format_convert_ms{0.0};
    double color_space_convert_ms{0.0};
    double encoder_ms{0.0};
    double pipe_write_ms{0.0};
    double backpressure_wait_ms{0.0};
    double pipe_write_cpu_ms{0.0};
    double pipe_backpressure_wait_ms{0.0};
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

    /// Current SceneProgramCache capacity at this frame.
    /// Populated by the render loop for per-frame trend charts.
    int program_cache_capacity{0};
};

struct PhaseTelemetryRecord {
    std::string phase_name;
    double duration_ms{0.0};
};

/// Canonical per-phase render pipeline breakdown (all values in milliseconds).
///
/// Five stable, non-overlapping phases that a GPU-resident overlay factory
/// cares about, so render speed is never conflated with encoding or I/O:
///
///   scene_eval_ms    graph/scene evaluation, easing, layout, scheduling —
///                    everything in the render loop that is not pixel work.
///   gpu_render_ms    actual node pixel execution (transform/composite/blur).
///   gpu_readback_ms  framebuffer → encoder conversion/copy (the readback).
///   encode_ms        codec encode of the readback frames.
///   disk_io_ms       writing encoded data to disk (pipe/mux + flush/close).
struct RenderPhaseTimings {
    static constexpr const char* kSceneEval   = "scene_eval_ms";
    static constexpr const char* kGpuRender   = "gpu_render_ms";
    static constexpr const char* kGpuReadback = "gpu_readback_ms";
    static constexpr const char* kEncode      = "encode_ms";
    static constexpr const char* kDiskIo      = "disk_io_ms";

    double scene_eval_ms{0.0};
    double gpu_render_ms{0.0};
    double gpu_readback_ms{0.0};
    double encode_ms{0.0};
    double disk_io_ms{0.0};

    [[nodiscard]] std::vector<PhaseTelemetryRecord> to_phase_records() const {
        return {
            {kSceneEval,   scene_eval_ms},
            {kGpuRender,   gpu_render_ms},
            {kGpuReadback, gpu_readback_ms},
            {kEncode,      encode_ms},
            {kDiskIo,      disk_io_ms},
        };
    }
};

struct CounterTelemetryRecord {
    std::string counter_name;
    uint64_t counter_value{0};
};

// ── End-of-run memory persistence (Stage 3, TICKET-TELEMETRY-SQLITE-NORMALIZATION) ──
// Projections built once at end-of-run by joining NodeMemoryTracker snapshots
// with node telemetry. Not measurement authorities; never written from the
// hot path.

/// One row of render_node_summary: per-node aggregate over the whole run.
struct NodeSummaryTelemetryRecord {
    std::string node_id;
    std::string node_type;
    std::string layer_id;

    uint64_t calls{0};
    double total_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};
    double avg_ms{0.0};

    uint64_t cache_hits{0};
    uint64_t cache_misses{0};

    // From NodeMemoryTracker (NodeStatsSnapshot):
    uint64_t pixels_read{0};
    uint64_t pixels_written{0};
    uint64_t bytes_read{0};
    uint64_t bytes_written{0};
    uint64_t allocations{0};
    uint64_t allocated_bytes{0};
    uint64_t temporary_buffers{0};
    uint64_t peak_live_bytes{0};
    uint64_t framebuffer_copies{0};
    uint64_t framebuffer_clears{0};

    uint64_t output_bytes{0};
};

/// One row of render_memory_summary: run-level memory envelope.
struct MemorySummaryTelemetryRecord {
    uint64_t peak_rss_bytes{0};

    uint64_t current_live_bytes{0};
    uint64_t peak_live_bytes{0};

    uint64_t framebuffer_current_bytes{0};
    uint64_t framebuffer_retained_bytes{0};
    uint64_t framebuffer_peak_retained_bytes{0};
    uint64_t framebuffer_allocations{0};
    uint64_t framebuffer_reuses{0};
    uint64_t framebuffer_returns{0};
    uint64_t framebuffer_evicted_bytes{0};
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

// ── Render artifact record (P0 video/text — Fase 1) ────────────────────────

/// Tracks every output artifact produced by a render run.
/// Written unconditionally (no --report flag required) so the telemetry
/// dashboard always shows what was produced.
struct RenderArtifactRecord {
    std::string run_id;
    std::string type;        // "png", "video", "frames", "audio"
    std::string path;        // absolute or relative output path
    std::string sha256;      // hex digest (computed lazily; empty = not computed)
    int64_t size_bytes{0};   // file size on disk (0 = file does not exist)
    bool file_exists{false}; // true if the file was found on disk after render
};

} // namespace chronon3d::telemetry
