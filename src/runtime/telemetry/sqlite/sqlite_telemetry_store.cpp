#include "sqlite_telemetry_store_impl.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace chronon3d::telemetry {

namespace {

// Detailed/Trace tables ONLY. Durable Summary rows (render_runs,
// render_counters, render_phase_events, summaries, artifacts) are never
// passed to the retention janitor.
constexpr const char* kDetailedTables[] = {
    "render_frames",
    "render_node_events",
    "render_layer_events",
    "render_cache_events",
    "render_culling_events",
    "render_image_events",
};

// UTC ISO-8601 "YYYY-MM-DDTHH:MM:SSZ" `days` ago — same fixed-width format
// as TelemetryManager::get_current_iso_time(), so a lexicographic comparison
// equals a chronological one for timestamps written by the engine.
std::string iso_time_days_ago(int days) {
    const auto cutoff =
        std::chrono::system_clock::now() - std::chrono::hours(24LL * days);
    const std::time_t t = std::chrono::system_clock::to_time_t(cutoff);
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

} // namespace

SqliteTelemetryStore::SqliteTelemetryStore()
    : m_impl(std::make_unique<Impl>()) {}

SqliteTelemetryStore::~SqliteTelemetryStore() = default;

void SqliteTelemetryStore::begin_transaction() {
    m_impl->mutex.lock();  // held until end_transaction(); write_* methods lock recursively
    if (m_impl->db) {
        exec_sql(m_impl->db, "BEGIN IMMEDIATE TRANSACTION;");
    }
}

void SqliteTelemetryStore::end_transaction(bool commit) {
    if (m_impl->db) {
        if (commit) {
            exec_sql(m_impl->db, "COMMIT;");
        } else {
            exec_sql(m_impl->db, "ROLLBACK;");
        }
    }
    m_impl->mutex.unlock();  // release the lock acquired in begin_transaction()
}

bool SqliteTelemetryStore::write_render_run(const RenderTelemetryRecord& run) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    // Named-column INSERT: column order matches the canonical telemetry columns (131 columns)
    const char* sql =
        "INSERT OR REPLACE INTO render_runs ("
        "run_id, composition_id, output_path, success, error_code, error_message, "
        "frames_total, frames_written, wall_time_ms, render_ms, encode_ms, "
        "effective_fps, pixels_touched, cache_hits, cache_misses, nodes_executed, "
        "layers_rendered, text_glyphs_rasterized, images_sampled, blur_pixels, "

        "simd_lerp_calls, "
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
        "framebuffer_acquire_wall_ms, framebuffer_clear_wall_ms, clearnode_wall_ms, "
        "clearnode_restore_wall_ms, "
        "clearnode_restore_rect_count, clearnode_restore_pixels, clearnode_restore_bytes, "
        "clearnode_restore_full_frame_count, clearnode_restore_dirty_rect_count, clearnode_restore_noop_count, "
        "framebuffer_pool_clear_wall_ms, framebuffer_enqueue_wall_ms, "
        "framebuffer_pool_empty_alloc, "
        "framebuffer_pool_best_fit_reuse, framebuffer_pool_exact_hit, framebuffer_buffer_returned_to_pool_count, "
        "framebuffer_pool_budget_bytes, framebuffer_pool_retained_bytes, "
        "framebuffer_pool_evicted_count, framebuffer_pool_evicted_bytes, "
        "framebuffer_pool_pressure_count, framebuffer_pool_size_class_count, "
        "unaligned_memory_copies, frame_conversion_copy_wall_ms, "
        "video_graph_eval_wall_ms, video_conversion_wall_ms, video_pipe_write_wall_ms, video_ffmpeg_wait_ms, "
        "io_queue_push_wait_ms, io_queue_pop_wait_ms, io_writer_idle_wait_ms, io_queue_peak_depth, ffmpeg_pipe_write_wall_ms, converted_frame_cache_hits, ffmpeg_flush_wall_ms, "        "io_queue_peak_bytes, setup_graph_parsing_wall_ms, setup_asset_io_load_wall_ms, setup_pool_preallocation_wall_ms, image_decode_wall_ms, "
        "compiled_graph_refresh_wall_ms, cache_eval_wall_ms, dirty_eval_wall_ms, input_resolve_wall_ms, "
        "predicted_bbox_wall_ms, clone_context_wall_ms, state_assign_wall_ms, "
        "framebuffer_lifetime_wall_ms, node_schedule_wall_ms, node_dispatch_wall_ms, "
        "node_execute_actual_wall_ms, node_overhead_wall_ms, telemetry_emit_wall_ms, "
        "chronon_render_only_ms, chronon_conversion_copy_ms, chronon_queue_wait_ms, "
        "chronon_render_throughput_ms, ffmpeg_encode_total_ms, ffmpeg_flush_close_ms, "
        "e2e_wall_ms, image_sample_ms, image_sampled_pixels, "
        "started_at_iso, finished_at_iso, git_commit_short, build_type, "
        "compiler_info, os, cpu_model, cores, "
        "logical_resource_count, physical_resource_slot_count, logical_resource_bytes, "
        "physical_resource_bytes, alias_saved_bytes, alias_reuse_count, "
        "new_resource_slot_count, arena_peak_bytes, "
        "ffprobe_wall_ms, sha256_wall_ms, process_startup_ms, "
        "framebuffer_allocations_per_frame"

        ") VALUES ("
        "?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, "
        "?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, "
        "?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, "
        "?31, ?32, ?33, ?34, ?35, ?36, ?37, ?38, ?39, ?40, "
        "?41, ?42, ?43, ?44, ?45, ?46, ?47, ?48, ?49, ?50, "
        "?51, ?52, ?53, ?54, ?55, ?56, ?57, ?58, ?59, ?60, "
        "?61, ?62, ?63, ?64, ?65, ?66, ?67, ?68, ?69, ?70, "
        "?71, ?72, ?73, ?74, ?75, ?76, ?77, ?78, ?79, ?80, "
        "?81, ?82, ?83, ?84, ?85, ?86, ?87, ?88, ?89, "
        "?90, ?91, ?92, ?93, ?94, ?95, ?96, ?97, ?98, "
        "?99, ?100, ?101, ?102, ?103, ?104, ?105, ?106, ?107, ?108, "
        "?109, ?110, ?111, ?112, ?113, ?114, "
        "?115, ?116, ?117, ?118, ?119, ?120, ?121, ?122, ?123, ?124, "
        "?125, ?126, ?127, ?128, "
        "?129, ?130, ?131"
        ");";

    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    return bind_all(stmt,
        run.run_id,
        run.composition_id,
        run.output_path,
        run.success,
        run.error_code,
        run.error_message,
        run.frames_total,
        run.frames_written,
        run.wall_time_ms,
        run.render_ms,
        run.encode_ms,
        run.effective_fps,
        run.pixels_touched,
        run.cache_hits,
        run.cache_misses,
        run.nodes_executed,
        run.layers_rendered,
        run.text_glyphs_rasterized,
        run.images_sampled,
        run.blur_pixels,

        run.simd_lerp_calls,
        run.bytes_allocated_peak,
        run.node_cache_hash_collisions,
        run.clear_skipped_calls,
        run.clear_skipped_pixels,
        run.clear_calls,
        run.clear_pixels,
        run.composite_calls,
        run.composite_pixels,
        run.transform_calls,
        run.transform_pixels,
        run.effect_stack_calls,
        run.effect_pixels,
        run.layer_culling_tests,
        run.layers_culled,
        run.layers_visible,
        run.framebuffer_allocations,
        run.framebuffer_reuses,
        run.framebuffer_bytes_allocated,
        run.framebuffer_bytes_peak,
        run.dirty_rect_count,
        run.dirty_pixels,
        run.dirty_union_area_pixels,
        run.dirty_full_fallbacks,
        run.bypass_not_cacheable_count,
        run.dirty_full_fallback_predicted_bounds_missing,
        run.dirty_full_fallback_composite_missing_input_bounds,
        run.dirty_full_fallback_transform_bounds_unknown,
        run.dirty_full_fallback_effect_bounds_unknown,
        run.framebuffer_acquire_wall_ms,
        run.framebuffer_clear_wall_ms,
        run.clearnode_wall_ms,
        run.clearnode_restore_wall_ms,
        run.clearnode_restore_rect_count,
        run.clearnode_restore_pixels,
        run.clearnode_restore_bytes,
        run.clearnode_restore_full_frame_count,
        run.clearnode_restore_dirty_rect_count,
        run.clearnode_restore_noop_count,
        run.framebuffer_pool_clear_wall_ms,
        run.framebuffer_enqueue_wall_ms,
        run.framebuffer_pool_empty_alloc,
        run.framebuffer_pool_best_fit_reuse,
        run.framebuffer_pool_exact_hit,
        run.framebuffer_buffer_returned_to_pool_count,
        run.framebuffer_pool_budget_bytes,
        run.framebuffer_pool_retained_bytes,
        run.framebuffer_pool_evicted_count,
        run.framebuffer_pool_evicted_bytes,
        run.framebuffer_pool_pressure_count,
        run.framebuffer_pool_size_class_count,
        run.unaligned_memory_copies,
        run.frame_conversion_copy_wall_ms,
        run.video_graph_eval_wall_ms,
        run.video_conversion_wall_ms,
        run.video_pipe_write_wall_ms,
        run.video_ffmpeg_wait_ms,
        run.io_queue_push_wait_ms,
        run.io_queue_pop_wait_ms,
        run.io_writer_idle_wait_ms,
        run.io_queue_peak_depth,
        run.ffmpeg_pipe_write_wall_ms,
        run.converted_frame_cache_hits,
        run.ffmpeg_flush_wall_ms,
        run.io_queue_peak_bytes,
        run.setup_graph_parsing_wall_ms,
        run.setup_asset_io_load_wall_ms,
        run.setup_pool_preallocation_wall_ms,
        run.image_decode_wall_ms,
        run.compiled_graph_refresh_wall_ms,
        run.cache_eval_wall_ms,
        run.dirty_eval_wall_ms,
        run.input_resolve_wall_ms,
        run.predicted_bbox_wall_ms,
        run.clone_context_wall_ms,
        run.state_assign_wall_ms,
        run.framebuffer_lifetime_wall_ms,
        run.node_schedule_wall_ms,
        run.node_dispatch_wall_ms,
        run.node_execute_actual_wall_ms,
        run.node_overhead_wall_ms,
        run.telemetry_emit_wall_ms,
        run.chronon_render_only_ms,
        run.chronon_conversion_copy_ms,
        run.chronon_queue_wait_ms,
        run.chronon_render_throughput_ms,
        run.ffmpeg_encode_total_ms,
        run.ffmpeg_flush_close_ms,
        run.e2e_wall_ms,
        run.image_sample_ms,
        run.image_sampled_pixels,
        run.started_at_iso,
        run.finished_at_iso,
        run.git_commit_short,
        run.build_type,
        run.compiler_info,
        run.os,
        run.cpu_model,
        run.cores,
        run.logical_resource_count,
        run.physical_resource_slot_count,
        run.logical_resource_bytes,
        run.physical_resource_bytes,
        run.alias_saved_bytes,
        run.alias_reuse_count,
        run.new_resource_slot_count,
        run.arena_peak_bytes,
        run.ffprobe_wall_ms,
        run.sha256_wall_ms,
        run.process_startup_ms,
        run.framebuffer_allocations_per_frame
    ) && stmt.step_done();
}

void SqliteTelemetryStore::apply_retention(int detail_ttl_days) {
    if (detail_ttl_days <= 0) return;
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return;

    // Purge granular rows whose run finished before the retention window.
    // Runs with an empty timestamp are conservatively kept (unknown age);
    // render_runs and the other Summary tables are never touched here.
    const std::string cutoff = iso_time_days_ago(detail_ttl_days);
    std::string sql = "BEGIN IMMEDIATE;";
    for (const char* table : kDetailedTables) {
        sql += " DELETE FROM ";
        sql += table;
        sql += " WHERE run_id IN (SELECT run_id FROM render_runs";
        sql += " WHERE finished_at_iso <> '' AND finished_at_iso < '";
        sql += cutoff;
        sql += "');";
    }
    sql += " COMMIT;";
    exec_sql(m_impl->db, sql.c_str());
}

} // namespace chronon3d::telemetry
