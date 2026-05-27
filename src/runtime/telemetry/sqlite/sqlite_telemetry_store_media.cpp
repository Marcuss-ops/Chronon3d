#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

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
