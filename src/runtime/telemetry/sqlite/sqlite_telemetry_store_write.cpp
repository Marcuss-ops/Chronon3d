#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

bool SqliteTelemetryStore::write_render_run(const RenderTelemetryRecord& run) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_runs VALUES ("
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?);";
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

    sqlite3_bind_int64(stmt, 28, run.clear_calls);
    sqlite3_bind_int64(stmt, 29, run.clear_pixels);
    sqlite3_bind_int64(stmt, 30, run.composite_calls);
    sqlite3_bind_int64(stmt, 31, run.composite_pixels);
    sqlite3_bind_int64(stmt, 32, run.transform_calls);
    sqlite3_bind_int64(stmt, 33, run.transform_pixels);
    sqlite3_bind_int64(stmt, 34, run.effect_stack_calls);
    sqlite3_bind_int64(stmt, 35, run.effect_pixels);
    sqlite3_bind_int64(stmt, 36, run.layer_culling_tests);
    sqlite3_bind_int64(stmt, 37, run.layers_culled);
    sqlite3_bind_int64(stmt, 38, run.layers_visible);
    sqlite3_bind_int64(stmt, 39, run.framebuffer_allocations);
    sqlite3_bind_int64(stmt, 40, run.framebuffer_reuses);
    sqlite3_bind_int64(stmt, 41, run.framebuffer_bytes_allocated);
    sqlite3_bind_int64(stmt, 42, run.framebuffer_bytes_peak);
    sqlite3_bind_int64(stmt, 43, run.dirty_rect_count);
    sqlite3_bind_int64(stmt, 44, run.dirty_pixels);
    sqlite3_bind_int64(stmt, 45, run.dirty_full_fallbacks);

    sqlite3_bind_int64(stmt, 46, run.dirty_full_fallback_predicted_bounds_missing);
    sqlite3_bind_int64(stmt, 47, run.dirty_full_fallback_composite_missing_input_bounds);
    sqlite3_bind_int64(stmt, 48, run.dirty_full_fallback_transform_bounds_unknown);
    sqlite3_bind_int64(stmt, 49, run.dirty_full_fallback_effect_bounds_unknown);

    sqlite3_bind_text(stmt, 50, run.started_at_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 51, run.finished_at_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 52, run.git_commit_short.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 53, run.build_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 54, run.compiler_info.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 55, run.os.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 56, run.cpu_model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 57, run.cores);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

} // namespace chronon3d::telemetry
#endif
