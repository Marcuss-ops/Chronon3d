#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

bool SqliteTelemetryStore::write_render_run(const RenderTelemetryRecord& run) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    // Named-column INSERT: column order matches telemetry_schema.sql exactly (59 columns)
    const char* sql =
        "INSERT OR REPLACE INTO render_runs ("
        "run_id, composition_id, output_path, success, error_code, error_message, "
        "frames_total, frames_written, wall_time_ms, render_ms, encode_ms, "
        "effective_fps, pixels_touched, cache_hits, cache_misses, nodes_executed, "
        "layers_rendered, text_glyphs_rasterized, images_sampled, blur_pixels, "
        "simd_lerp_calls, tiles_total, tiles_hit, tiles_miss, tiles_partial, "
        "bytes_allocated_peak, node_cache_hash_collisions, "
        "clear_calls, clear_pixels, composite_calls, composite_pixels, "
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
        "started_at_iso, finished_at_iso, git_commit_short, build_type, "
        "compiler_info, os, cpu_model, cores"
        ") VALUES ("
        "?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, "
        "?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, "
        "?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, "
        "?31, ?32, ?33, ?34, ?35, ?36, ?37, ?38, ?39, ?40, "
        "?41, ?42, ?43, ?44, ?45, ?46, ?47, ?48, ?49, ?50, "
        "?51, ?52, ?53, ?54, ?55, ?56, ?57, ?58, ?59"
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
    sqlite3_bind_int64(stmt, 45, run.dirty_union_area_pixels);
    sqlite3_bind_int64(stmt, 46, run.dirty_full_fallbacks);
    sqlite3_bind_int64(stmt, 47, run.bypass_not_cacheable_count);

    sqlite3_bind_int64(stmt, 48, run.dirty_full_fallback_predicted_bounds_missing);
    sqlite3_bind_int64(stmt, 49, run.dirty_full_fallback_composite_missing_input_bounds);
    sqlite3_bind_int64(stmt, 50, run.dirty_full_fallback_transform_bounds_unknown);
    sqlite3_bind_int64(stmt, 51, run.dirty_full_fallback_effect_bounds_unknown);

    sqlite3_bind_text(stmt, 52, run.started_at_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 53, run.finished_at_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 54, run.git_commit_short.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 55, run.build_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 56, run.compiler_info.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 57, run.os.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 58, run.cpu_model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 59, run.cores);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

} // namespace chronon3d::telemetry
#endif
