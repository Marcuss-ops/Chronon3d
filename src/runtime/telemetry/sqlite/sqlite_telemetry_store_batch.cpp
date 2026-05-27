#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

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

} // namespace chronon3d::telemetry
#endif
