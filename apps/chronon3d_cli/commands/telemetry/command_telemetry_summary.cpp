#include "command_telemetry_internal.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include <sqlite3.h>
#include <string>

namespace chronon3d::cli {

RunSummary query_run_summary(sqlite3* db, const std::string& run_id) {
    RunSummary run;
    const char* run_sql =
        "SELECT composition_id, output_path, success, frames_total, frames_written, wall_time_ms, render_ms, encode_ms, effective_fps, "
        "pixels_touched, cache_hits, cache_misses, nodes_executed, layers_rendered, text_glyphs_rasterized, images_sampled, blur_pixels, "
        "simd_lerp_calls, tiles_total, tiles_hit, tiles_miss, tiles_partial, bytes_allocated_peak, node_cache_hash_collisions, "
        "clear_skipped_calls, clear_skipped_pixels, clear_calls, clear_pixels, composite_calls, composite_pixels, transform_calls, transform_pixels, effect_stack_calls, effect_pixels, "
        "layer_culling_tests, layers_culled, layers_visible, framebuffer_allocations, framebuffer_reuses, framebuffer_bytes_allocated, "
        "framebuffer_bytes_peak, started_at_iso, finished_at_iso, git_commit_short, build_type, compiler_info, os, cpu_model, cores, "
        "dirty_rect_count, dirty_pixels, dirty_full_fallbacks "
        "FROM render_runs WHERE run_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (prepare_with_run_id(db, &stmt, run_sql, run_id) && sqlite3_step(stmt) == SQLITE_ROW) {
        run.composition_id = sql_text(stmt, 0);
        run.output_path = sql_text(stmt, 1);
        run.success = sqlite3_column_int(stmt, 2) != 0;
        run.frames_total = static_cast<int>(sql_i64(stmt, 3));
        run.frames_written = static_cast<int>(sql_i64(stmt, 4));
        run.wall_time_ms = sql_double(stmt, 5);
        run.render_ms = sql_double(stmt, 6);
        run.encode_ms = sql_double(stmt, 7);
        run.effective_fps = sql_double(stmt, 8);
        run.pixels_touched = static_cast<uint64_t>(sql_i64(stmt, 9));
        run.cache_hits = static_cast<uint64_t>(sql_i64(stmt, 10));
        run.cache_misses = static_cast<uint64_t>(sql_i64(stmt, 11));
        run.nodes_executed = static_cast<uint64_t>(sql_i64(stmt, 12));
        run.layers_rendered = static_cast<uint64_t>(sql_i64(stmt, 13));
        run.text_glyphs_rasterized = static_cast<uint64_t>(sql_i64(stmt, 14));
        run.images_sampled = static_cast<uint64_t>(sql_i64(stmt, 15));
        run.blur_pixels = static_cast<uint64_t>(sql_i64(stmt, 16));
        run.simd_lerp_calls = static_cast<uint64_t>(sql_i64(stmt, 17));
        run.tiles_total = static_cast<uint64_t>(sql_i64(stmt, 18));
        run.tiles_hit = static_cast<uint64_t>(sql_i64(stmt, 19));
        run.tiles_miss = static_cast<uint64_t>(sql_i64(stmt, 20));
        run.tiles_partial = static_cast<uint64_t>(sql_i64(stmt, 21));
        run.bytes_allocated_peak = static_cast<uint64_t>(sql_i64(stmt, 22));
        run.node_cache_hash_collisions = static_cast<uint64_t>(sql_i64(stmt, 23));
        run.clear_skipped_calls = static_cast<uint64_t>(sql_i64(stmt, 24));
        run.clear_skipped_pixels = static_cast<uint64_t>(sql_i64(stmt, 25));
        run.clear_calls = static_cast<uint64_t>(sql_i64(stmt, 26));
        run.clear_pixels = static_cast<uint64_t>(sql_i64(stmt, 27));
        run.composite_calls = static_cast<uint64_t>(sql_i64(stmt, 28));
        run.composite_pixels = static_cast<uint64_t>(sql_i64(stmt, 29));
        run.transform_calls = static_cast<uint64_t>(sql_i64(stmt, 30));
        run.transform_pixels = static_cast<uint64_t>(sql_i64(stmt, 31));
        run.effect_stack_calls = static_cast<uint64_t>(sql_i64(stmt, 32));
        run.effect_pixels = static_cast<uint64_t>(sql_i64(stmt, 33));
        run.layer_culling_tests = static_cast<uint64_t>(sql_i64(stmt, 34));
        run.layers_culled = static_cast<uint64_t>(sql_i64(stmt, 35));
        run.layers_visible = static_cast<uint64_t>(sql_i64(stmt, 36));
        run.framebuffer_allocations = static_cast<uint64_t>(sql_i64(stmt, 37));
        run.framebuffer_reuses = static_cast<uint64_t>(sql_i64(stmt, 38));
        run.framebuffer_bytes_allocated = static_cast<uint64_t>(sql_i64(stmt, 39));
        run.framebuffer_bytes_peak = static_cast<uint64_t>(sql_i64(stmt, 40));
        run.started_at_iso = sql_text(stmt, 41);
        run.finished_at_iso = sql_text(stmt, 42);
        run.git_commit_short = sql_text(stmt, 43);
        run.build_type = sql_text(stmt, 44);
        run.compiler_info = sql_text(stmt, 45);
        run.os = sql_text(stmt, 46);
        run.cpu_model = sql_text(stmt, 47);
        run.cores = static_cast<int>(sql_i64(stmt, 48));
        run.dirty_rect_count = static_cast<uint64_t>(sql_i64(stmt, 49));
        run.dirty_pixels = static_cast<uint64_t>(sql_i64(stmt, 50));
        run.dirty_full_fallbacks = static_cast<uint64_t>(sql_i64(stmt, 51));
    }
    sqlite3_finalize(stmt);
    return run;
}

} // namespace chronon3d::cli
#endif
