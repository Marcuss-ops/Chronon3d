#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

bool SqliteTelemetryStore::write_node_events(const std::string& run_id, const std::vector<NodeTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_node_events "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool success = true;
    for (const auto& ev : events) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, ev.frame_number);
        sqlite3_bind_text(stmt, 3, ev.node_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, ev.node_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, ev.layer_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 6, ev.duration_ms);
        sqlite3_bind_text(stmt, 7, ev.cache_status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, ev.cache_key_digest.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 9, ev.input_count);
        sqlite3_bind_int(stmt, 10, ev.output_width);
        sqlite3_bind_int(stmt, 11, ev.output_height);
        sqlite3_bind_int64(stmt, 12, ev.output_bytes);
        sqlite3_bind_double(stmt, 13, ev.bbox_x);
        sqlite3_bind_double(stmt, 14, ev.bbox_y);
        sqlite3_bind_double(stmt, 15, ev.bbox_w);
        sqlite3_bind_double(stmt, 16, ev.bbox_h);
        sqlite3_bind_double(stmt, 17, ev.visible_x);
        sqlite3_bind_double(stmt, 18, ev.visible_y);
        sqlite3_bind_double(stmt, 19, ev.visible_w);
        sqlite3_bind_double(stmt, 20, ev.visible_h);
        sqlite3_bind_int64(stmt, 21, ev.pixels_touched);
        sqlite3_bind_int64(stmt, 22, ev.pixels_cleared);
        sqlite3_bind_int64(stmt, 23, ev.pixels_composited);
        sqlite3_bind_int64(stmt, 24, ev.pixels_transformed);
        sqlite3_bind_int64(stmt, 25, ev.pixels_blurred);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return success;
}

bool SqliteTelemetryStore::write_layer_events(const std::string& run_id, const std::vector<LayerTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_layer_events "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool success = true;
    for (const auto& ev : events) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, ev.frame_number);
        sqlite3_bind_text(stmt, 3, ev.layer_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, ev.layer_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, ev.layer_type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 6, ev.duration_ms);
        sqlite3_bind_int(stmt, 7, ev.visible ? 1 : 0);
        sqlite3_bind_text(stmt, 8, ev.cull_reason.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 9, ev.opacity);
        sqlite3_bind_text(stmt, 10, ev.blend_mode.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 11, ev.bbox_x);
        sqlite3_bind_double(stmt, 12, ev.bbox_y);
        sqlite3_bind_double(stmt, 13, ev.bbox_w);
        sqlite3_bind_double(stmt, 14, ev.bbox_h);
        sqlite3_bind_double(stmt, 15, ev.visible_x);
        sqlite3_bind_double(stmt, 16, ev.visible_y);
        sqlite3_bind_double(stmt, 17, ev.visible_w);
        sqlite3_bind_double(stmt, 18, ev.visible_h);
        sqlite3_bind_int(stmt, 19, ev.area_pixels);
        sqlite3_bind_int(stmt, 20, ev.visible_pixels);
        sqlite3_bind_int(stmt, 21, ev.dirty_pixels);
        sqlite3_bind_text(stmt, 22, ev.effects.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 23, ev.effect_padding);
        sqlite3_bind_int(stmt, 24, ev.glyphs_rasterized);
        sqlite3_bind_int(stmt, 25, ev.images_sampled);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return success;
}

bool SqliteTelemetryStore::write_cache_events(const std::string& run_id, const std::vector<CacheTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_cache_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool success = true;
    for (const auto& ev : events) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, ev.frame_number);
        sqlite3_bind_text(stmt, 3, ev.node_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, ev.cacheable ? 1 : 0);
        sqlite3_bind_text(stmt, 5, ev.cache_status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, ev.key_digest.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, ev.params_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, ev.source_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, ev.input_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 10, ev.output_bytes);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return success;
}

bool SqliteTelemetryStore::write_culling_events(const std::string& run_id, const std::vector<CullingTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_culling_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool success = true;
    for (const auto& ev : events) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, ev.frame_number);
        sqlite3_bind_text(stmt, 3, ev.layer_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, ev.visible ? 1 : 0);
        sqlite3_bind_text(stmt, 5, ev.reason.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 6, ev.bbox_x);
        sqlite3_bind_double(stmt, 7, ev.bbox_y);
        sqlite3_bind_double(stmt, 8, ev.bbox_w);
        sqlite3_bind_double(stmt, 9, ev.bbox_h);
        sqlite3_bind_double(stmt, 10, ev.visible_x);
        sqlite3_bind_double(stmt, 11, ev.visible_y);
        sqlite3_bind_double(stmt, 12, ev.visible_w);
        sqlite3_bind_double(stmt, 13, ev.visible_h);
        sqlite3_bind_int64(stmt, 14, ev.saved_pixels);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return success;
}

} // namespace chronon3d::telemetry
#endif
