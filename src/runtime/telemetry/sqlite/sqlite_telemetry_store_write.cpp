#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

bool SqliteTelemetryStore::write_render_run(const RenderTelemetryRecord& run) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    // Named-column INSERT: column order matches telemetry_schema.sql exactly (87 columns)
    const char* sql =
        "INSERT OR REPLACE INTO render_runs ("
        "run_id, composition_id, output_path, success, error_code, error_message, "
        "frames_total, frames_written, wall_time_ms, render_ms, encode_ms, "
        "effective_fps, pixels_touched, cache_hits, cache_misses, nodes_executed, "
        "layers_rendered, text_glyphs_rasterized, images_sampled, blur_pixels, "
        "simd_lerp_calls, tiles_total, tiles_hit, tiles_miss, tiles_partial, "
        "bytes_allocated_peak, node_cache_hash_collisions, "
        "clear_skipped_calls, clear_skipped_pixels, clear_calls, clear_pixels, composite_calls, composite_pixels, "
        "transform_calls, transform_pixels, effect_stack_calls, effect_pixels, "
        "layer_culling_tests, layers_culled, layers_visible, "
        "framebuffer_allocations, framebuffer_reuses, framebuffer_bytes_allocated, "
        "framebuffer_bytes_peak, "
        "dirty_rect_count, dirty_pixels, dirty_union_area_pixels, dirty_full_fallbacks, "
        "bypass_not_cacheable_count, "
        "dirty_full_fallback_predicted_bounds_missing, "
        "dirty_full_fallback_composite_missing_input_bounds, "
        "dirty_full_fallback_transform_bounds_unknown, "
        "dirty_full_fallback_effect_bounds_unknown, "
        "framebuffer_acquire_ms, framebuffer_clear_ms, clearnode_ms, "
        "framebuffer_pool_clear_ms, framebuffer_enqueue_ms, "
        "framebuffer_pool_miss_count_size_mismatch, framebuffer_pool_miss_count_empty, "
        "framebuffer_pool_hits, framebuffer_buffer_returned_to_pool_count, "
        "unaligned_memory_copies, frame_conversion_copy_ms, "
        "video_graph_eval_ms, video_conversion_ms, video_pipe_write_ms, video_ffmpeg_latency_ms, "
        "io_queue_push_blocked_ms, io_queue_pop_wait_ms, io_queue_peak_depth, ffmpeg_pipe_write_blocked_ms, ffmpeg_flush_ms, "
        "chronon_render_only_ms, chronon_conversion_copy_ms, chronon_queue_wait_ms, "
        "chronon_render_throughput_ms, ffmpeg_encode_total_ms, ffmpeg_flush_close_ms, "
        "e2e_wall_ms, "
        "started_at_iso, finished_at_iso, git_commit_short, build_type, "
        "compiler_info, os, cpu_model, cores"
        ") VALUES ("
        "?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, "
        "?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, "
        "?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, "
        "?31, ?32, ?33, ?34, ?35, ?36, ?37, ?38, ?39, ?40, "
        "?41, ?42, ?43, ?44, ?45, ?46, ?47, ?48, ?49, ?50, "
        "?51, ?52, ?53, ?54, ?55, ?56, ?57, ?58, ?59, ?60, "
        "?61, ?62, ?63, ?64, ?65, ?66, ?67, ?68, ?69, ?70, "
        "?71, ?72, ?73, ?74, ?75, ?76, ?77, ?78, ?79, ?80, "
        "?81, ?82, ?83, ?84, ?85, ?86, ?87, ?88"
        ");";
    sqlite3_stmt* stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, run.run_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, run.composition_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, run.output_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, run.success ? 1 : 0);
    sqlite3_bind_text(stmt, 5, run.error_code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, run.error_message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, run.frames_total);
    sqlite3_bind_int(stmt, 8, run.frames_written);
    sqlite3_bind_double(stmt, 9, run.wall_time_ms);
    sqlite3_bind_double(stmt, 10, run.render_ms);
    sqlite3_bind_double(stmt, 11, run.encode_ms);
    sqlite3_bind_double(stmt, 12, run.effective_fps);
    sqlite3_bind_int64(stmt, 13, run.pixels_touched);
    sqlite3_bind_int64(stmt, 14, run.cache_hits);
    sqlite3_bind_int64(stmt, 15, run.cache_misses);
    sqlite3_bind_int64(stmt, 16, run.nodes_executed);
    sqlite3_bind_int64(stmt, 17, run.layers_rendered);
    sqlite3_bind_int64(stmt, 18, run.text_glyphs_rasterized);
    sqlite3_bind_int64(stmt, 19, run.images_sampled);
    sqlite3_bind_int64(stmt, 20, run.blur_pixels);
    sqlite3_bind_int64(stmt, 21, run.simd_lerp_calls);
    sqlite3_bind_int64(stmt, 22, run.tiles_total);
    sqlite3_bind_int64(stmt, 23, run.tiles_hit);
    sqlite3_bind_int64(stmt, 24, run.tiles_miss);
    sqlite3_bind_int64(stmt, 25, run.tiles_partial);
    sqlite3_bind_int64(stmt, 26, run.bytes_allocated_peak);
    sqlite3_bind_int64(stmt, 27, run.node_cache_hash_collisions);

    sqlite3_bind_int64(stmt, 28, run.clear_skipped_calls);
    sqlite3_bind_int64(stmt, 29, run.clear_skipped_pixels);
    sqlite3_bind_int64(stmt, 30, run.clear_calls);
    sqlite3_bind_int64(stmt, 31, run.clear_pixels);
    sqlite3_bind_int64(stmt, 32, run.composite_calls);
    sqlite3_bind_int64(stmt, 33, run.composite_pixels);
    sqlite3_bind_int64(stmt, 34, run.transform_calls);
    sqlite3_bind_int64(stmt, 35, run.transform_pixels);
    sqlite3_bind_int64(stmt, 36, run.effect_stack_calls);
    sqlite3_bind_int64(stmt, 37, run.effect_pixels);
    sqlite3_bind_int64(stmt, 38, run.layer_culling_tests);
    sqlite3_bind_int64(stmt, 39, run.layers_culled);
    sqlite3_bind_int64(stmt, 40, run.layers_visible);
    sqlite3_bind_int64(stmt, 41, run.framebuffer_allocations);
    sqlite3_bind_int64(stmt, 42, run.framebuffer_reuses);
    sqlite3_bind_int64(stmt, 43, run.framebuffer_bytes_allocated);
    sqlite3_bind_int64(stmt, 44, run.framebuffer_bytes_peak);
    sqlite3_bind_int64(stmt, 45, run.dirty_rect_count);
    sqlite3_bind_int64(stmt, 46, run.dirty_pixels);
    sqlite3_bind_int64(stmt, 47, run.dirty_union_area_pixels);
    sqlite3_bind_int64(stmt, 48, run.dirty_full_fallbacks);
    sqlite3_bind_int64(stmt, 49, run.bypass_not_cacheable_count);

    sqlite3_bind_int64(stmt, 50, run.dirty_full_fallback_predicted_bounds_missing);
    sqlite3_bind_int64(stmt, 51, run.dirty_full_fallback_composite_missing_input_bounds);
    sqlite3_bind_int64(stmt, 52, run.dirty_full_fallback_transform_bounds_unknown);
    sqlite3_bind_int64(stmt, 53, run.dirty_full_fallback_effect_bounds_unknown);

    // Framebuffer / pipeline timing counters (19 new columns, ?54-?72)
    sqlite3_bind_int64(stmt, 54, run.framebuffer_acquire_ms);
    sqlite3_bind_int64(stmt, 55, run.framebuffer_clear_ms);
    sqlite3_bind_int64(stmt, 56, run.clearnode_ms);
    sqlite3_bind_int64(stmt, 57, run.framebuffer_pool_clear_ms);
    sqlite3_bind_int64(stmt, 58, run.framebuffer_enqueue_ms);
    sqlite3_bind_int64(stmt, 59, run.framebuffer_pool_miss_count_size_mismatch);
    sqlite3_bind_int64(stmt, 60, run.framebuffer_pool_miss_count_empty);
    sqlite3_bind_int64(stmt, 61, run.framebuffer_pool_hits);
    sqlite3_bind_int64(stmt, 62, run.framebuffer_buffer_returned_to_pool_count);
    sqlite3_bind_int64(stmt, 63, run.unaligned_memory_copies);
    sqlite3_bind_int64(stmt, 64, run.frame_conversion_copy_ms);
    sqlite3_bind_int64(stmt, 65, run.video_graph_eval_ms);
    sqlite3_bind_int64(stmt, 66, run.video_conversion_ms);
    sqlite3_bind_int64(stmt, 67, run.video_pipe_write_ms);
    sqlite3_bind_int64(stmt, 68, run.video_ffmpeg_latency_ms);
    sqlite3_bind_int64(stmt, 69, run.io_queue_push_blocked_ms);
    sqlite3_bind_int64(stmt, 70, run.io_queue_pop_wait_ms);
    sqlite3_bind_int64(stmt, 71, run.io_queue_peak_depth);
    sqlite3_bind_int64(stmt, 72, run.ffmpeg_pipe_write_blocked_ms);
    sqlite3_bind_int64(stmt, 73, run.ffmpeg_flush_ms);

    // Benchmark breakdown columns (shifted: were ?54-?60, now ?74-?80)
    sqlite3_bind_double(stmt, 74, run.chronon_render_only_ms);
    sqlite3_bind_double(stmt, 75, run.chronon_conversion_copy_ms);
    sqlite3_bind_double(stmt, 76, run.chronon_queue_wait_ms);
    sqlite3_bind_double(stmt, 77, run.chronon_render_throughput_ms);
    sqlite3_bind_double(stmt, 78, run.ffmpeg_encode_total_ms);
    sqlite3_bind_double(stmt, 79, run.ffmpeg_flush_close_ms);
    sqlite3_bind_double(stmt, 80, run.e2e_wall_ms);

    // Host & environment (shifted: were ?61-?68, now ?81-?88)
    sqlite3_bind_text(stmt, 81, run.started_at_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 82, run.finished_at_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 83, run.git_commit_short.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 84, run.build_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 85, run.compiler_info.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 86, run.os.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 87, run.cpu_model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 88, run.cores);


    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

} // namespace chronon3d::telemetry
#endif
