#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

bool SqliteTelemetryStore::write_text_events(const std::string& run_id, const std::vector<TextTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT INTO render_text_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
        sqlite3_bind_int(stmt, 4, ev.text_length);
        sqlite3_bind_int(stmt, 5, ev.line_count);
        sqlite3_bind_int(stmt, 6, ev.glyph_count);
        sqlite3_bind_int(stmt, 7, ev.glyphs_rasterized);
        sqlite3_bind_int(stmt, 8, ev.glyph_cache_hits);
        sqlite3_bind_int(stmt, 9, ev.glyph_cache_misses);
        sqlite3_bind_double(stmt, 10, ev.layout_ms);
        sqlite3_bind_double(stmt, 11, ev.raster_ms);
        sqlite3_bind_double(stmt, 12, ev.composite_ms);
        sqlite3_bind_text(stmt, 13, ev.font_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 14, ev.font_size);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return success;
}

bool SqliteTelemetryStore::write_image_events(const std::string& run_id, const std::vector<ImageTelemetryRecord>& events) {
    std::scoped_lock lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT INTO render_image_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
        sqlite3_bind_text(stmt, 4, ev.image_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, ev.image_width);
        sqlite3_bind_int(stmt, 6, ev.image_height);
        sqlite3_bind_text(stmt, 7, ev.cache_status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 8, ev.decode_ms);
        sqlite3_bind_double(stmt, 9, ev.sample_ms);
        sqlite3_bind_int64(stmt, 10, ev.sampled_pixels);

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
