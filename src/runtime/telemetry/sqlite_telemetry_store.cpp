#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>
#include <filesystem>
#include <mutex>

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>
#endif

namespace chronon3d::telemetry {

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY

struct SqliteTelemetryStore::Impl {
    sqlite3* db{nullptr};
    std::mutex mutex;

    ~Impl() {
        close();
    }

    void close() {
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
    }
};

SqliteTelemetryStore::SqliteTelemetryStore()
    : m_impl(std::make_unique<Impl>()) {}

SqliteTelemetryStore::~SqliteTelemetryStore() = default;

bool SqliteTelemetryStore::initialize(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->close();

    std::filesystem::path fs_path(db_path);
    if (fs_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }

    int rc = sqlite3_open(db_path.c_str(), &m_impl->db);
    if (rc != SQLITE_OK) {
        m_impl->close();
        return false;
    }

    // Set performance PRAGMAs
    sqlite3_exec(m_impl->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_impl->db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    // Self-healing schema migration: Check if render_runs exists and has fewer columns than expected (35)
    bool needs_recreate = false;
    sqlite3_stmt* check_stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, "PRAGMA table_info(render_runs);", -1, &check_stmt, nullptr) == SQLITE_OK) {
        int cols = 0;
        while (sqlite3_step(check_stmt) == SQLITE_ROW) {
            cols++;
        }
        sqlite3_finalize(check_stmt);
        if (cols > 0 && cols < 35) {
            needs_recreate = true;
        }
    }

    if (needs_recreate) {
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_runs;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_frames;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_phase_events;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_counters;", nullptr, nullptr, nullptr);
    }

    // Create tables
    const char* schema = 
        "CREATE TABLE IF NOT EXISTS render_runs (\n"
        "    run_id TEXT PRIMARY KEY,\n"
        "    composition_id TEXT,\n"
        "    output_path TEXT,\n"
        "    success INTEGER,\n"
        "    error_code TEXT,\n"
        "    error_message TEXT,\n"
        "    frames_total INTEGER,\n"
        "    frames_written INTEGER,\n"
        "    wall_time_ms REAL,\n"
        "    render_ms REAL,\n"
        "    encode_ms REAL,\n"
        "    effective_fps REAL,\n"
        "    pixels_touched INTEGER,\n"
        "    cache_hits INTEGER,\n"
        "    cache_misses INTEGER,\n"
        "    nodes_executed INTEGER,\n"
        "    layers_rendered INTEGER,\n"
        "    text_glyphs_rasterized INTEGER,\n"
        "    images_sampled INTEGER,\n"
        "    blur_pixels INTEGER,\n"
        "    simd_lerp_calls INTEGER,\n"
        "    tiles_total INTEGER,\n"
        "    tiles_hit INTEGER,\n"
        "    tiles_miss INTEGER,\n"
        "    tiles_partial INTEGER,\n"
        "    bytes_allocated_peak INTEGER,\n"
        "    node_cache_hash_collisions INTEGER,\n"
        "    started_at_iso TEXT,\n"
        "    finished_at_iso TEXT,\n"
        "    git_commit_short TEXT,\n"
        "    build_type TEXT,\n"
        "    compiler_info TEXT,\n"
        "    os TEXT,\n"
        "    cpu_model TEXT,\n"
        "    cores INTEGER\n"
        ");\n"
        "\n"
        "CREATE TABLE IF NOT EXISTS render_frames (\n"
        "    run_id TEXT,\n"
        "    frame_number INTEGER,\n"
        "    duration_ms REAL,\n"
        "    cache_hit INTEGER,\n"
        "    dirty_area_ratio REAL,\n"
        "    PRIMARY KEY (run_id, frame_number)\n"
        ");\n"
        "\n"
        "CREATE TABLE IF NOT EXISTS render_phase_events (\n"
        "    run_id TEXT,\n"
        "    phase_name TEXT,\n"
        "    duration_ms REAL,\n"
        "    PRIMARY KEY (run_id, phase_name)\n"
        ");\n"
        "\n"
        "CREATE TABLE IF NOT EXISTS render_counters (\n"
        "    run_id TEXT,\n"
        "    counter_name TEXT,\n"
        "    counter_value INTEGER,\n"
        "    PRIMARY KEY (run_id, counter_name)\n"
        ");\n";

    char* err_msg = nullptr;
    rc = sqlite3_exec(m_impl->db, schema, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool SqliteTelemetryStore::write_render_run(const RenderTelemetryRecord& run) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    const char* sql = "INSERT OR REPLACE INTO render_runs VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
    sqlite3_bind_text(stmt, 28, run.started_at_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 29, run.finished_at_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 30, run.git_commit_short.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 31, run.build_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 32, run.compiler_info.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 33, run.os.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 34, run.cpu_model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 35, run.cores);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SqliteTelemetryStore::write_frames(const std::string& run_id, const std::vector<FrameTelemetryRecord>& frames) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    // Use transaction for batch performance
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

#else

struct SqliteTelemetryStore::Impl {};

// Optional SQLite telemetry is disabled; stub out everything gracefully.
SqliteTelemetryStore::SqliteTelemetryStore()
    : m_impl(std::make_unique<Impl>()) {}
SqliteTelemetryStore::~SqliteTelemetryStore() = default;

bool SqliteTelemetryStore::initialize(const std::string&) { return false; }
bool SqliteTelemetryStore::write_render_run(const RenderTelemetryRecord&) { return false; }
bool SqliteTelemetryStore::write_frames(const std::string&, const std::vector<FrameTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_phases(const std::string&, const std::vector<PhaseTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_counters(const std::string&, const std::vector<CounterTelemetryRecord>&) { return false; }

#endif

} // namespace chronon3d::telemetry
