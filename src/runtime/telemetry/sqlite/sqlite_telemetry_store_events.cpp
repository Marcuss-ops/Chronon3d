#include "sqlite_telemetry_store_impl.hpp"

namespace chronon3d::telemetry {

bool SqliteTelemetryStore::write_frames(const std::string& run_id, const std::vector<FrameTelemetry>& frames) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql =
        "INSERT OR REPLACE INTO render_frames ("
        "run_id, frame_number, duration_ms, cache_hit, dirty_area_ratio, "
        "graph_eval_ms, queue_wait_ms, conversion_copy_ms, encoder_ms, pipe_write_ms, "
        "native_convert_ms, native_send_ms, native_receive_ms, native_mux_ms, "
        "dirty_rect_enabled, dirty_rect_x0, dirty_rect_y0, dirty_rect_x1, dirty_rect_y1, "
        "tile_execution_used, fast_path_reused, graph_reused"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& frame : frames) {
        if (!stmt.reset() || !bind_all(stmt,
                run_id, frame.frame_number, frame.duration_ms,
                static_cast<int>(frame.cache_hit), frame.dirty_area_ratio,
                frame.graph_eval_ms, frame.queue_wait_ms, frame.conversion_copy_ms,
                frame.encoder_ms, frame.pipe_write_ms,
                frame.native_convert_ms, frame.native_send_ms, frame.native_receive_ms, frame.native_mux_ms,
                static_cast<int>(frame.dirty_rect_enabled),
                frame.dirty_rect_x0, frame.dirty_rect_y0, frame.dirty_rect_x1, frame.dirty_rect_y1,
                static_cast<int>(frame.tile_execution_used),
                static_cast<int>(frame.fast_path_reused),
                static_cast<int>(frame.graph_reused)) || !stmt.step_done()) {
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

    const char* sql = "INSERT OR REPLACE INTO render_cache_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
                ev.output_bytes,
                ev.key_width,
                ev.key_height,
                ev.key_frame,
                ev.key_tile_x,
                ev.key_tile_y,
                ev.key_tile_size,
                ev.key_tile_hash) || !stmt.step_done()) {
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

bool SqliteTelemetryStore::write_artifacts(const std::string& run_id, const std::vector<RenderArtifactRecord>& artifacts) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_artifacts "
        "(run_id, type, path, sha256, size_bytes, file_exists) "
        "VALUES (?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& a : artifacts) {
        if (!stmt.reset() || !bind_all(stmt,
                run_id,
                a.type,
                a.path,
                a.sha256,
                a.size_bytes,
                static_cast<int>(a.file_exists)) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

// Stage 3 — end-of-run memory persistence projections.
bool SqliteTelemetryStore::write_node_summaries(const std::string& run_id,
                                                const std::vector<NodeSummaryTelemetryRecord>& summaries) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_node_summary "
        "(run_id, node_id, node_type, layer_id, "
        "calls, total_ms, min_ms, max_ms, avg_ms, "
        "cache_hits, cache_misses, "
        "pixels_read, pixels_written, bytes_read, bytes_written, "
        "allocations, allocated_bytes, temporary_buffers, peak_live_bytes, "
        "framebuffer_copies, framebuffer_clears, output_bytes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    for (const auto& s : summaries) {
        if (!stmt.reset() || !bind_all(stmt,
                run_id,
                s.node_id,
                s.node_type,
                s.layer_id,
                s.calls,
                s.total_ms,
                s.min_ms,
                s.max_ms,
                s.avg_ms,
                s.cache_hits,
                s.cache_misses,
                s.pixels_read,
                s.pixels_written,
                s.bytes_read,
                s.bytes_written,
                s.allocations,
                s.allocated_bytes,
                s.temporary_buffers,
                s.peak_live_bytes,
                s.framebuffer_copies,
                s.framebuffer_clears,
                s.output_bytes) || !stmt.step_done()) {
            return false;
        }
    }

    return true;
}

bool SqliteTelemetryStore::write_memory_summary(const std::string& run_id,
                                                const MemorySummaryTelemetryRecord& summary) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_memory_summary "
        "(run_id, peak_rss_bytes, current_live_bytes, peak_live_bytes, "
        "framebuffer_current_bytes, framebuffer_retained_bytes, "
        "framebuffer_peak_retained_bytes, framebuffer_allocations, "
        "framebuffer_reuses, framebuffer_returns, framebuffer_evicted_bytes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    SqliteStatement stmt(m_impl->db, sql);
    if (!stmt) {
        return false;
    }

    return bind_all(stmt,
        run_id,
        summary.peak_rss_bytes,
        summary.current_live_bytes,
        summary.peak_live_bytes,
        summary.framebuffer_current_bytes,
        summary.framebuffer_retained_bytes,
        summary.framebuffer_peak_retained_bytes,
        summary.framebuffer_allocations,
        summary.framebuffer_reuses,
        summary.framebuffer_returns,
        summary.framebuffer_evicted_bytes) && stmt.step_done();
}

} // namespace chronon3d::telemetry
