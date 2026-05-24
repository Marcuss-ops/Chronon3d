#pragma once

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include <sqlite3.h>
#include <cstdint>
#include <string>

namespace chronon3d::cli {

// Shared RunSummary struct
struct RunSummary {
    std::string composition_id;
    std::string output_path;
    std::string started_at_iso;
    std::string finished_at_iso;
    std::string git_commit_short;
    std::string build_type;
    std::string compiler_info;
    std::string os;
    std::string cpu_model;
    bool success{false};
    int frames_total{0};
    int frames_written{0};
    int cores{0};
    double wall_time_ms{0.0};
    double render_ms{0.0};
    double encode_ms{0.0};
    double effective_fps{0.0};
    uint64_t pixels_touched{0};
    uint64_t cache_hits{0};
    uint64_t cache_misses{0};
    uint64_t nodes_executed{0};
    uint64_t layers_rendered{0};
    uint64_t text_glyphs_rasterized{0};
    uint64_t images_sampled{0};
    uint64_t blur_pixels{0};
    uint64_t simd_lerp_calls{0};
    uint64_t tiles_total{0};
    uint64_t tiles_hit{0};
    uint64_t tiles_miss{0};
    uint64_t tiles_partial{0};
    uint64_t bytes_allocated_peak{0};
    uint64_t node_cache_hash_collisions{0};
    uint64_t clear_skipped_calls{0};
    uint64_t clear_skipped_pixels{0};
    uint64_t clear_calls{0};
    uint64_t clear_pixels{0};
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
    uint64_t dirty_full_fallbacks{0};
    uint64_t framebuffer_acquire_ms{0};
    uint64_t framebuffer_clear_ms{0};
    uint64_t clearnode_ms{0};
    uint64_t framebuffer_pool_clear_ms{0};
    uint64_t framebuffer_enqueue_ms{0};
    uint64_t framebuffer_pool_miss_count_size_mismatch{0};
    uint64_t framebuffer_pool_miss_count_empty{0};
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
    uint64_t io_queue_peak_depth{0};
    uint64_t ffmpeg_pipe_write_blocked_ms{0};
    uint64_t converted_frame_cache_hits{0};
    uint64_t ffmpeg_flush_ms{0};
    uint64_t io_queue_peak_bytes{0};

    // Setup deep dive
    uint64_t setup_graph_parsing_ms{0};
    uint64_t setup_asset_io_load_ms{0};
    uint64_t setup_pool_preallocation_ms{0};
    uint64_t image_decode_ms{0};

    // OS & Process diagnostics
    uint64_t process_context_switches_voluntary{0};
    uint64_t process_context_switches_involuntary{0};
    uint64_t os_page_faults_major{0};
    uint64_t os_page_faults_minor{0};
    uint64_t ffmpeg_cpu_user_pct{0};
    uint64_t ffmpeg_cpu_sys_pct{0};
    uint64_t llc_references{0};
    uint64_t llc_misses{0};
};

// SQL helper declarations
std::string sql_text(sqlite3_stmt* stmt, int col);
int64_t sql_i64(sqlite3_stmt* stmt, int col);
double sql_double(sqlite3_stmt* stmt, int col);
std::string format_pct(double value);
std::string format_bytes(uint64_t bytes);
std::string format_ms(double value);
bool prepare_with_run_id(sqlite3* db, sqlite3_stmt** stmt, const char* sql, const std::string& run_id);

// RunSummary query
RunSummary query_run_summary(sqlite3* db, const std::string& run_id);

// Report generation
void generate_telemetry_report(std::stringstream& out, sqlite3* db, const std::string& run_id, const RunSummary& run);

} // namespace chronon3d::cli
#endif
