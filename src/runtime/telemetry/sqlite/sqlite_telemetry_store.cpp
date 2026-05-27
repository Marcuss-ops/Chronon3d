#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

SqliteTelemetryStore::SqliteTelemetryStore()
    : m_impl(std::make_unique<Impl>()) {}

SqliteTelemetryStore::~SqliteTelemetryStore() = default;

void SqliteTelemetryStore::begin_transaction() {
    m_impl->mutex.lock();  // held until end_transaction(); write_* methods lock recursively
    if (m_impl->db) {
        exec_sql(m_impl->db, "BEGIN TRANSACTION;");
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

    // Named-column INSERT: column order matches telemetry_schema.sql exactly (91 columns)
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
        "framebuffer_acquire_ms, framebuffer_clear_ms, clearnode_ms, "
        "framebuffer_pool_clear_ms, framebuffer_enqueue_ms, "
        "framebuffer_pool_miss_count_size_mismatch, framebuffer_pool_miss_count_empty, "
        "framebuffer_pool_hits, framebuffer_buffer_returned_to_pool_count, "
        "unaligned_memory_copies, frame_conversion_copy_ms, "
        "video_graph_eval_ms, video_conversion_ms, video_pipe_write_ms, video_ffmpeg_latency_ms, "
        "io_queue_push_blocked_ms, io_queue_pop_wait_ms, io_queue_peak_depth, ffmpeg_pipe_write_blocked_ms, converted_frame_cache_hits, ffmpeg_flush_ms, "
        "io_queue_peak_bytes, setup_graph_parsing_ms, setup_asset_io_load_ms, setup_pool_preallocation_ms, image_decode_ms, "
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
        "?81, ?82, ?83, ?84, ?85, ?86, ?87, ?88, ?89, "
        "?90"
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
        run.framebuffer_acquire_ms,
        run.framebuffer_clear_ms,
        run.clearnode_ms,
        run.framebuffer_pool_clear_ms,
        run.framebuffer_enqueue_ms,
        run.framebuffer_pool_miss_count_size_mismatch,
        run.framebuffer_pool_miss_count_empty,
        run.framebuffer_pool_hits,
        run.framebuffer_buffer_returned_to_pool_count,
        run.unaligned_memory_copies,
        run.frame_conversion_copy_ms,
        run.video_graph_eval_ms,
        run.video_conversion_ms,
        run.video_pipe_write_ms,
        run.video_ffmpeg_latency_ms,
        run.io_queue_push_blocked_ms,
        run.io_queue_pop_wait_ms,
        run.io_queue_peak_depth,
        run.ffmpeg_pipe_write_blocked_ms,
        run.converted_frame_cache_hits,
        run.ffmpeg_flush_ms,
        run.io_queue_peak_bytes,
        run.setup_graph_parsing_ms,
        run.setup_asset_io_load_ms,
        run.setup_pool_preallocation_ms,
        run.image_decode_ms,
        run.chronon_render_only_ms,
        run.chronon_conversion_copy_ms,
        run.chronon_queue_wait_ms,
        run.chronon_render_throughput_ms,
        run.ffmpeg_encode_total_ms,
        run.ffmpeg_flush_close_ms,
        run.e2e_wall_ms,
        run.started_at_iso,
        run.finished_at_iso,
        run.git_commit_short,
        run.build_type,
        run.compiler_info,
        run.os,
        run.cpu_model,
        run.cores
    ) && stmt.step_done();
}

bool SqliteTelemetryStore::write_frames(const std::string& run_id, const std::vector<FrameTelemetryRecord>& frames) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_frames VALUES (?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& frame : frames) {
        if (!stmt.reset() || !bind_all(stmt, run_id, frame.frame_number, frame.duration_ms, frame.cache_hit, frame.dirty_area_ratio) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

bool SqliteTelemetryStore::write_phases(const std::string& run_id, const std::vector<PhaseTelemetryRecord>& phases) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_phase_events VALUES (?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& phase : phases) {
        if (!stmt.reset() || !bind_all(stmt, run_id, phase.phase_name, phase.duration_ms) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

bool SqliteTelemetryStore::write_counters(const std::string& run_id, const std::vector<CounterTelemetryRecord>& counters) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_counters VALUES (?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& cnt : counters) {
        if (!stmt.reset() || !bind_all(stmt, run_id, cnt.counter_name, cnt.counter_value) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

bool SqliteTelemetryStore::write_tile_events(const std::string& run_id, const std::vector<TileTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_tile_events VALUES (?, ?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& ev : events) {
        if (!stmt.reset() || !bind_all(stmt, run_id, ev.frame_number, ev.layer_id, ev.tile_x, ev.tile_y, ev.tile_status, ev.dirty_rects_count) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

bool SqliteTelemetryStore::write_node_events(const std::string& run_id, const std::vector<NodeTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_node_events "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& ev : events) {
        if (!stmt.reset() || !bind_all(stmt,
                run_id,
                ev.frame_number,
                ev.node_name,
                ev.node_type,
                ev.layer_id,
                ev.duration_ms,
                ev.cache_status,
                ev.cache_key_digest,
                ev.input_count,
                ev.output_width,
                ev.output_height,
                ev.output_bytes,
                ev.bbox_x,
                ev.bbox_y,
                ev.bbox_w,
                ev.bbox_h,
                ev.visible_x,
                ev.visible_y,
                ev.visible_w,
                ev.visible_h,
                ev.pixels_touched,
                ev.pixels_cleared,
                ev.pixels_composited,
                ev.pixels_transformed,
                ev.pixels_blurred) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

bool SqliteTelemetryStore::write_layer_events(const std::string& run_id, const std::vector<LayerTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_layer_events "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& ev : events) {
        if (!stmt.reset() || !bind_all(stmt,
                run_id,
                ev.frame_number,
                ev.layer_id,
                ev.layer_name,
                ev.layer_type,
                ev.duration_ms,
                ev.visible,
                ev.cull_reason,
                ev.opacity,
                ev.blend_mode,
                ev.bbox_x,
                ev.bbox_y,
                ev.bbox_w,
                ev.bbox_h,
                ev.visible_x,
                ev.visible_y,
                ev.visible_w,
                ev.visible_h,
                ev.area_pixels,
                ev.visible_pixels,
                ev.dirty_pixels,
                ev.effects,
                ev.effect_padding,
                ev.glyphs_rasterized,
                ev.images_sampled) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

bool SqliteTelemetryStore::write_cache_events(const std::string& run_id, const std::vector<CacheTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_cache_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& ev : events) {
        if (!stmt.reset() || !bind_all(stmt,
                run_id,
                ev.frame_number,
                ev.node_name,
                ev.cacheable,
                ev.cache_status,
                ev.key_digest,
                ev.params_hash,
                ev.source_hash,
                ev.input_hash,
                ev.output_bytes) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

bool SqliteTelemetryStore::write_culling_events(const std::string& run_id, const std::vector<CullingTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_culling_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& ev : events) {
        if (!stmt.reset() || !bind_all(stmt,
                run_id,
                ev.frame_number,
                ev.layer_id,
                ev.visible,
                ev.reason,
                ev.bbox_x,
                ev.bbox_y,
                ev.bbox_w,
                ev.bbox_h,
                ev.visible_x,
                ev.visible_y,
                ev.visible_w,
                ev.visible_h,
                ev.saved_pixels) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

bool SqliteTelemetryStore::write_text_events(const std::string& run_id, const std::vector<TextTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT INTO render_text_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& ev : events) {
        if (!stmt.reset() || !bind_all(stmt,
                run_id,
                ev.frame_number,
                ev.layer_id,
                ev.text_length,
                ev.line_count,
                ev.glyph_count,
                ev.glyphs_rasterized,
                ev.glyph_cache_hits,
                ev.glyph_cache_misses,
                ev.layout_ms,
                ev.raster_ms,
                ev.composite_ms,
                ev.font_path,
                ev.font_size) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

bool SqliteTelemetryStore::write_image_events(const std::string& run_id, const std::vector<ImageTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT INTO render_image_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& ev : events) {
        if (!stmt.reset() || !bind_all(stmt,
                run_id,
                ev.frame_number,
                ev.layer_id,
                ev.image_path,
                ev.image_width,
                ev.image_height,
                ev.cache_status,
                ev.decode_ms,
                ev.sample_ms,
                ev.sampled_pixels) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

} // namespace chronon3d::telemetry
#endif
