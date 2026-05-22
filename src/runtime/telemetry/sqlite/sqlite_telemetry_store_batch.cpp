#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

bool SqliteTelemetryStore::write_frames(const std::string& run_id, const std::vector<FrameTelemetryRecord>& frames) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    sqlite3_exec(m_impl->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT OR REPLACE INTO render_frames VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    bool success = true;
    for (const auto& frame : frames) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, frame.frame_number);
        sqlite3_bind_double(stmt, 3, frame.duration_ms);
        sqlite3_bind_int(stmt, 4, frame.cache_hit ? 1 : 0);
        sqlite3_bind_double(stmt, 5, frame.dirty_area_ratio);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    if (success) {
        sqlite3_exec(m_impl->db, "COMMIT;", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return success;
}

bool SqliteTelemetryStore::write_phases(const std::string& run_id, const std::vector<PhaseTelemetryRecord>& phases) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    sqlite3_exec(m_impl->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT OR REPLACE INTO render_phase_events VALUES (?, ?, ?);";
    sqlite3_stmt* stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    bool success = true;
    for (const auto& phase : phases) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, phase.phase_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, phase.duration_ms);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    if (success) {
        sqlite3_exec(m_impl->db, "COMMIT;", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return success;
}

bool SqliteTelemetryStore::write_counters(const std::string& run_id, const std::vector<CounterTelemetryRecord>& counters) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    sqlite3_exec(m_impl->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT OR REPLACE INTO render_counters VALUES (?, ?, ?);";
    sqlite3_stmt* stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    bool success = true;
    for (const auto& cnt : counters) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, cnt.counter_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, cnt.counter_value);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    if (success) {
        sqlite3_exec(m_impl->db, "COMMIT;", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return success;
}

bool SqliteTelemetryStore::write_tile_events(const std::string& run_id, const std::vector<TileTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    sqlite3_exec(m_impl->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT OR REPLACE INTO render_tile_events VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    bool success = true;
    for (const auto& ev : events) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, ev.frame_number);
        sqlite3_bind_text(stmt, 3, ev.layer_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, ev.tile_x);
        sqlite3_bind_int(stmt, 5, ev.tile_y);
        sqlite3_bind_text(stmt, 6, ev.tile_status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 7, ev.dirty_rects_count);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    if (success) {
        sqlite3_exec(m_impl->db, "COMMIT;", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return success;
}

} // namespace chronon3d::telemetry
#endif
