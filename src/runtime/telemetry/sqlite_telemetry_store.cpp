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

    // Self-healing schema migration: Check if render_runs exists and has fewer columns than expected (53)
    bool needs_recreate = false;
    sqlite3_stmt* check_stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, "PRAGMA table_info(render_runs);", -1, &check_stmt, nullptr) == SQLITE_OK) {
        int cols = 0;
        while (sqlite3_step(check_stmt) == SQLITE_ROW) {
            cols++;
        }
        sqlite3_finalize(check_stmt);
        if (cols > 0 && cols < 53) {
            needs_recreate = true;
        }
    }

    if (needs_recreate) {
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_runs;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_frames;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_phase_events;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_counters;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_node_events;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_layer_events;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_cache_events;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_culling_events;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_text_events;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_image_events;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_impl->db, "DROP TABLE IF EXISTS render_tile_events;", nullptr, nullptr, nullptr);
    }

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
        "    clear_calls INTEGER,\n"
        "    clear_pixels INTEGER,\n"
        "    composite_calls INTEGER,\n"
        "    composite_pixels INTEGER,\n"
        "    transform_calls INTEGER,\n"
        "    transform_pixels INTEGER,\n"
        "    effect_stack_calls INTEGER,\n"
        "    effect_pixels INTEGER,\n"
        "    layer_culling_tests INTEGER,\n"
        "    layers_culled INTEGER,\n"
        "    layers_visible INTEGER,\n"
        "    framebuffer_allocations INTEGER,\n"
        "    framebuffer_reuses INTEGER,\n"
        "    framebuffer_bytes_allocated INTEGER,\n"
        "    framebuffer_bytes_peak INTEGER,\n"
        "    dirty_rect_count INTEGER,\n"
        "    dirty_pixels INTEGER,\n"
        "    dirty_full_fallbacks INTEGER,\n"
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
        ");\n"
        "\n"
        "CREATE TABLE IF NOT EXISTS render_node_events (\n"
        "    run_id TEXT,\n"
        "    frame_number INTEGER,\n"
        "    node_name TEXT,\n"
        "    node_type TEXT,\n"
        "    layer_id TEXT,\n"
        "    duration_ms REAL,\n"
        "    cache_status TEXT,\n"
        "    cache_key_digest TEXT,\n"
        "    input_count INTEGER,\n"
        "    output_width INTEGER,\n"
        "    output_height INTEGER,\n"
        "    output_bytes INTEGER,\n"
        "    bbox_x REAL,\n"
        "    bbox_y REAL,\n"
        "    bbox_w REAL,\n"
        "    bbox_h REAL,\n"
        "    visible_x REAL,\n"
        "    visible_y REAL,\n"
        "    visible_w REAL,\n"
        "    visible_h REAL,\n"
        "    pixels_touched INTEGER,\n"
        "    pixels_cleared INTEGER,\n"
        "    pixels_composited INTEGER,\n"
        "    pixels_transformed INTEGER,\n"
        "    pixels_blurred INTEGER,\n"
        "    PRIMARY KEY (run_id, frame_number, node_name, node_type)\n"
        ");\n"
        "\n"
        "CREATE TABLE IF NOT EXISTS render_layer_events (\n"
        "    run_id TEXT,\n"
        "    frame_number INTEGER,\n"
        "    layer_id TEXT,\n"
        "    layer_name TEXT,\n"
        "    layer_type TEXT,\n"
        "    duration_ms REAL,\n"
        "    visible INTEGER,\n"
        "    cull_reason TEXT,\n"
        "    opacity REAL,\n"
        "    blend_mode TEXT,\n"
        "    bbox_x REAL,\n"
        "    bbox_y REAL,\n"
        "    bbox_w REAL,\n"
        "    bbox_h REAL,\n"
        "    visible_x REAL,\n"
        "    visible_y REAL,\n"
        "    visible_w REAL,\n"
        "    visible_h REAL,\n"
        "    area_pixels INTEGER,\n"
        "    visible_pixels INTEGER,\n"
        "    dirty_pixels INTEGER,\n"
        "    effects TEXT,\n"
        "    effect_padding REAL,\n"
        "    glyphs_rasterized INTEGER,\n"
        "    images_sampled INTEGER,\n"
        "    PRIMARY KEY (run_id, frame_number, layer_id)\n"
        ");\n"
        "\n"
        "CREATE TABLE IF NOT EXISTS render_cache_events (\n"
        "    run_id TEXT,\n"
        "    frame_number INTEGER,\n"
        "    node_name TEXT,\n"
        "    cacheable INTEGER,\n"
        "    cache_status TEXT,\n"
        "    key_digest TEXT,\n"
        "    params_hash TEXT,\n"
        "    source_hash TEXT,\n"
        "    input_hash TEXT,\n"
        "    output_bytes INTEGER,\n"
        "    PRIMARY KEY (run_id, frame_number, node_name)\n"
        ");\n"
        "\n"
        "CREATE TABLE IF NOT EXISTS render_culling_events (\n"
        "    run_id TEXT,\n"
        "    frame_number INTEGER,\n"
        "    layer_id TEXT,\n"
        "    visible INTEGER,\n"
        "    reason TEXT,\n"
        "    bbox_x REAL,\n"
        "    bbox_y REAL,\n"
        "    bbox_w REAL,\n"
        "    bbox_h REAL,\n"
        "    visible_x REAL,\n"
        "    visible_y REAL,\n"
        "    visible_w REAL,\n"
        "    visible_h REAL,\n"
        "    saved_pixels INTEGER,\n"
        "    PRIMARY KEY (run_id, frame_number, layer_id)\n"
        ");\n"
        "\n"
        "CREATE TABLE IF NOT EXISTS render_text_events (\n"
        "    run_id TEXT,\n"
        "    frame_number INTEGER,\n"
        "    layer_id TEXT,\n"
        "    text_length INTEGER,\n"
        "    line_count INTEGER,\n"
        "    glyph_count INTEGER,\n"
        "    glyphs_rasterized INTEGER,\n"
        "    glyph_cache_hits INTEGER,\n"
        "    glyph_cache_misses INTEGER,\n"
        "    layout_ms REAL,\n"
        "    raster_ms REAL,\n"
        "    composite_ms REAL,\n"
        "    font_path TEXT,\n"
        "    font_size REAL\n"
        ");\n"
        "\n"
        "CREATE TABLE IF NOT EXISTS render_image_events (\n"
        "    run_id TEXT,\n"
        "    frame_number INTEGER,\n"
        "    layer_id TEXT,\n"
        "    image_path TEXT,\n"
        "    image_width INTEGER,\n"
        "    image_height INTEGER,\n"
        "    cache_status TEXT,\n"
        "    decode_ms REAL,\n"
        "    sample_ms REAL,\n"
        "    sampled_pixels INTEGER\n"
        ");\n"
        "\n"
        "CREATE TABLE IF NOT EXISTS render_tile_events (\n"
        "    run_id TEXT,\n"
        "    frame_number INTEGER,\n"
        "    layer_id TEXT,\n"
        "    tile_x INTEGER,\n"
        "    tile_y INTEGER,\n"
        "    tile_status TEXT,\n"
        "    dirty_rects_count INTEGER,\n"
        "    PRIMARY KEY (run_id, frame_number, layer_id, tile_x, tile_y)\n"
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

    const char* sql = "INSERT OR REPLACE INTO render_runs VALUES ("
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?);"; // 53 ?s
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

    sqlite3_bind_int64(stmt, 28, run.clear_calls);
    sqlite3_bind_int64(stmt, 29, run.clear_pixels);
    sqlite3_bind_int64(stmt, 30, run.composite_calls);
    sqlite3_bind_int64(stmt, 31, run.composite_pixels);
    sqlite3_bind_int64(stmt, 32, run.transform_calls);
    sqlite3_bind_int64(stmt, 33, run.transform_pixels);
    sqlite3_bind_int64(stmt, 34, run.effect_stack_calls);
    sqlite3_bind_int64(stmt, 35, run.effect_pixels);
    sqlite3_bind_int64(stmt, 36, run.layer_culling_tests);
    sqlite3_bind_int64(stmt, 37, run.layers_culled);
    sqlite3_bind_int64(stmt, 38, run.layers_visible);
    sqlite3_bind_int64(stmt, 39, run.framebuffer_allocations);
    sqlite3_bind_int64(stmt, 40, run.framebuffer_reuses);
    sqlite3_bind_int64(stmt, 41, run.framebuffer_bytes_allocated);
    sqlite3_bind_int64(stmt, 42, run.framebuffer_bytes_peak);
    sqlite3_bind_int64(stmt, 43, run.dirty_rect_count);
    sqlite3_bind_int64(stmt, 44, run.dirty_pixels);
    sqlite3_bind_int64(stmt, 45, run.dirty_full_fallbacks);

    sqlite3_bind_text(stmt, 46, run.started_at_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 47, run.finished_at_iso.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 48, run.git_commit_short.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 49, run.build_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 50, run.compiler_info.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 51, run.os.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 52, run.cpu_model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 53, run.cores);

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

bool SqliteTelemetryStore::write_node_events(const std::string& run_id, const std::vector<NodeTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    sqlite3_exec(m_impl->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT OR REPLACE INTO render_node_events "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
    if (success) {
        sqlite3_exec(m_impl->db, "COMMIT;", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return success;
}

bool SqliteTelemetryStore::write_layer_events(const std::string& run_id, const std::vector<LayerTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    sqlite3_exec(m_impl->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT OR REPLACE INTO render_layer_events "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
    if (success) {
        sqlite3_exec(m_impl->db, "COMMIT;", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return success;
}

bool SqliteTelemetryStore::write_cache_events(const std::string& run_id, const std::vector<CacheTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    sqlite3_exec(m_impl->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT OR REPLACE INTO render_cache_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
    if (success) {
        sqlite3_exec(m_impl->db, "COMMIT;", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return success;
}

bool SqliteTelemetryStore::write_culling_events(const std::string& run_id, const std::vector<CullingTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    sqlite3_exec(m_impl->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT OR REPLACE INTO render_culling_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
    if (success) {
        sqlite3_exec(m_impl->db, "COMMIT;", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return success;
}

bool SqliteTelemetryStore::write_text_events(const std::string& run_id, const std::vector<TextTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    sqlite3_exec(m_impl->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT INTO render_text_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
    if (success) {
        sqlite3_exec(m_impl->db, "COMMIT;", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(m_impl->db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return success;
}

bool SqliteTelemetryStore::write_image_events(const std::string& run_id, const std::vector<ImageTelemetryRecord>& events) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->db) return false;

    sqlite3_exec(m_impl->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* sql = "INSERT INTO render_image_events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
bool SqliteTelemetryStore::write_node_events(const std::string&, const std::vector<NodeTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_layer_events(const std::string&, const std::vector<LayerTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_cache_events(const std::string&, const std::vector<CacheTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_culling_events(const std::string&, const std::vector<CullingTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_text_events(const std::string&, const std::vector<TextTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_image_events(const std::string&, const std::vector<ImageTelemetryRecord>&) { return false; }
bool SqliteTelemetryStore::write_tile_events(const std::string&, const std::vector<TileTelemetryRecord>&) { return false; }

#endif

} // namespace chronon3d::telemetry
