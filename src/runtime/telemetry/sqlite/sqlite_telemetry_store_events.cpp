#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

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
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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

} // namespace chronon3d::telemetry
#endif
