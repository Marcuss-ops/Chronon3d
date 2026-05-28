#include "command_telemetry_internal.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include <sqlite3.h>
#include <string>
#include <unordered_map>

namespace chronon3d::cli {

RunSummary query_run_summary(sqlite3* db, const std::string& run_id) {
    RunSummary run;
    const char* run_sql = "SELECT * FROM render_runs WHERE run_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_with_run_id(db, &stmt, run_sql, run_id) && sqlite3_step(stmt) == SQLITE_ROW) {
        std::unordered_map<std::string, int> col_map;
        int col_count = sqlite3_column_count(stmt);
        for (int i = 0; i < col_count; ++i) {
            const char* name = sqlite3_column_name(stmt, i);
            if (name) {
                col_map[name] = i;
            }
        }

        auto get_str = [&](const std::string& name) -> std::string {
            auto it = col_map.find(name);
            return it != col_map.end() ? sql_text(stmt, it->second) : "";
        };
        auto get_i64 = [&](const std::string& name) -> uint64_t {
            auto it = col_map.find(name);
            return it != col_map.end() ? static_cast<uint64_t>(sql_i64(stmt, it->second)) : 0;
        };
        auto get_double = [&](const std::string& name) -> double {
            auto it = col_map.find(name);
            return it != col_map.end() ? sql_double(stmt, it->second) : 0.0;
        };

        run.run_id = run_id;
        run.composition_id = get_str("composition_id");
        run.output_path = get_str("output_path");
        run.success = get_i64("success") != 0;
        run.error_code = get_str("error_code");
        run.error_message = get_str("error_message");
        run.frames_total = static_cast<int>(get_i64("frames_total"));
        run.frames_written = static_cast<int>(get_i64("frames_written"));
        run.wall_time_ms = get_double("wall_time_ms");
        run.render_ms = get_double("render_ms");
        run.encode_ms = get_double("encode_ms");
        run.effective_fps = get_double("effective_fps");

        run.pixels_touched = get_i64("pixels_touched");
        run.cache_hits = get_i64("cache_hits");
        run.cache_misses = get_i64("cache_misses");
        run.nodes_executed = get_i64("nodes_executed");
        run.layers_rendered = get_i64("layers_rendered");
        run.text_glyphs_rasterized = get_i64("text_glyphs_rasterized");
        run.images_sampled = get_i64("images_sampled");
        run.blur_pixels = get_i64("blur_pixels");
        run.simd_lerp_calls = get_i64("simd_lerp_calls");

        run.bytes_allocated_peak = get_i64("bytes_allocated_peak");
        run.node_cache_hash_collisions = get_i64("node_cache_hash_collisions");
        run.clear_skipped_calls = get_i64("clear_skipped_calls");
        run.clear_skipped_pixels = get_i64("clear_skipped_pixels");
        run.clear_calls = get_i64("clear_calls");
        run.clear_pixels = get_i64("clear_pixels");
        run.composite_calls = get_i64("composite_calls");
        run.composite_pixels = get_i64("composite_pixels");
        run.transform_calls = get_i64("transform_calls");
        run.transform_pixels = get_i64("transform_pixels");
        run.effect_stack_calls = get_i64("effect_stack_calls");
        run.effect_pixels = get_i64("effect_pixels");
        run.layer_culling_tests = get_i64("layer_culling_tests");
        run.layers_culled = get_i64("layers_culled");
        run.layers_visible = get_i64("layers_visible");

        run.framebuffer_allocations = get_i64("framebuffer_allocations");
        run.framebuffer_reuses = get_i64("framebuffer_reuses");
        run.framebuffer_bytes_allocated = get_i64("framebuffer_bytes_allocated");
        run.framebuffer_bytes_peak = get_i64("framebuffer_bytes_peak");
        run.dirty_rect_count = get_i64("dirty_rect_count");
        run.dirty_pixels = get_i64("dirty_pixels");
        run.dirty_union_area_pixels = get_i64("dirty_union_area_pixels");
        run.dirty_full_fallbacks = get_i64("dirty_full_fallbacks");
        run.bypass_not_cacheable_count = get_i64("bypass_not_cacheable_count");

        run.dirty_full_fallback_predicted_bounds_missing = get_i64("dirty_full_fallback_predicted_bounds_missing");
        run.dirty_full_fallback_composite_missing_input_bounds = get_i64("dirty_full_fallback_composite_missing_input_bounds");
        run.dirty_full_fallback_transform_bounds_unknown = get_i64("dirty_full_fallback_transform_bounds_unknown");
        run.dirty_full_fallback_effect_bounds_unknown = get_i64("dirty_full_fallback_effect_bounds_unknown");

        run.framebuffer_acquire_ms = get_i64("framebuffer_acquire_ms");
        run.framebuffer_clear_ms = get_i64("framebuffer_clear_ms");
        run.clearnode_ms = get_i64("clearnode_ms");
        run.framebuffer_pool_clear_ms = get_i64("framebuffer_pool_clear_ms");
        run.framebuffer_enqueue_ms = get_i64("framebuffer_enqueue_ms");
        run.framebuffer_pool_miss_count_size_mismatch = get_i64("framebuffer_pool_miss_count_size_mismatch");
        run.framebuffer_pool_miss_count_empty = get_i64("framebuffer_pool_miss_count_empty");
        run.framebuffer_pool_miss_count_best_fit = get_i64("framebuffer_pool_miss_count_best_fit");
        run.framebuffer_pool_hits = get_i64("framebuffer_pool_hits");
        run.framebuffer_buffer_returned_to_pool_count = get_i64("framebuffer_buffer_returned_to_pool_count");
        run.unaligned_memory_copies = get_i64("unaligned_memory_copies");
        run.frame_conversion_copy_ms = get_i64("frame_conversion_copy_ms");
        run.video_graph_eval_ms = get_i64("video_graph_eval_ms");
        run.video_conversion_ms = get_i64("video_conversion_ms");
        run.video_pipe_write_ms = get_i64("video_pipe_write_ms");
        run.video_ffmpeg_latency_ms = get_i64("video_ffmpeg_latency_ms");

        run.io_queue_push_blocked_ms = get_i64("io_queue_push_blocked_ms");
        run.io_queue_pop_wait_ms = get_i64("io_queue_pop_wait_ms");
        run.io_writer_idle_wait_ms = get_i64("io_writer_idle_wait_ms");
        run.io_queue_peak_depth = get_i64("io_queue_peak_depth");
        run.ffmpeg_pipe_write_blocked_ms = get_i64("ffmpeg_pipe_write_blocked_ms");
        run.converted_frame_cache_hits = get_i64("converted_frame_cache_hits");
        run.ffmpeg_flush_ms = get_i64("ffmpeg_flush_ms");
        run.io_queue_peak_bytes = get_i64("io_queue_peak_bytes");

        run.setup_graph_parsing_ms = get_i64("setup_graph_parsing_ms");
        run.setup_asset_io_load_ms = get_i64("setup_asset_io_load_ms");
        run.setup_pool_preallocation_ms = get_i64("setup_pool_preallocation_ms");
        run.image_decode_ms = get_i64("image_decode_ms");

        run.chronon_render_only_ms = get_double("chronon_render_only_ms");
        run.chronon_conversion_copy_ms = get_double("chronon_conversion_copy_ms");
        run.chronon_queue_wait_ms = get_double("chronon_queue_wait_ms");
        run.chronon_render_throughput_ms = get_double("chronon_render_throughput_ms");
        run.ffmpeg_encode_total_ms = get_double("ffmpeg_encode_total_ms");
        run.ffmpeg_flush_close_ms = get_double("ffmpeg_flush_close_ms");
        run.e2e_wall_ms = get_double("e2e_wall_ms");

        run.started_at_iso = get_str("started_at_iso");
        run.finished_at_iso = get_str("finished_at_iso");
        run.git_commit_short = get_str("git_commit_short");
        run.build_type = get_str("build_type");
        run.compiler_info = get_str("compiler_info");
        run.os = get_str("os");
        run.cpu_model = get_str("cpu_model");
        run.cores = static_cast<int>(get_i64("cores"));
    }
    sqlite3_finalize(stmt);

    // Query generic / diagnostic counters from render_counters table
    sqlite3_stmt* counter_stmt = nullptr;
    const char* counter_sql = "SELECT counter_name, counter_value FROM render_counters WHERE run_id = ?;";
    if (prepare_with_run_id(db, &counter_stmt, counter_sql, run_id)) {
        while (sqlite3_step(counter_stmt) == SQLITE_ROW) {
            std::string name = sql_text(counter_stmt, 0);
            uint64_t val = static_cast<uint64_t>(sql_i64(counter_stmt, 1));
            if (name == "tiles_total") run.tiles_total = val;
            else if (name == "tiles_hit") run.tiles_hit = val;
            else if (name == "tiles_miss") run.tiles_miss = val;
            else if (name == "tiles_partial") run.tiles_partial = val;
            else if (name == "process_context_switches_voluntary") run.process_context_switches_voluntary = val;
            else if (name == "process_context_switches_involuntary") run.process_context_switches_involuntary = val;
            else if (name == "os_page_faults_major") run.os_page_faults_major = val;
            else if (name == "os_page_faults_minor") run.os_page_faults_minor = val;
            else if (name == "ffmpeg_cpu_user_pct") run.ffmpeg_cpu_user_pct = val;
            else if (name == "ffmpeg_cpu_sys_pct") run.ffmpeg_cpu_sys_pct = val;
            else if (name == "llc_references") run.llc_references = val;
            else if (name == "llc_misses") run.llc_misses = val;
        }
        sqlite3_finalize(counter_stmt);
    }

    return run;
}

} // namespace chronon3d::cli
#endif
