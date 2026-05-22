#include "sqlite_telemetry_store_impl.hpp"
#include <filesystem>

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

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

    // Self-healing schema migration: Check if render_runs exists and has fewer columns than expected (57)
    bool needs_recreate = false;
    sqlite3_stmt* check_stmt{nullptr};
    if (sqlite3_prepare_v2(m_impl->db, "PRAGMA table_info(render_runs);", -1, &check_stmt, nullptr) == SQLITE_OK) {
        int cols = 0;
        while (sqlite3_step(check_stmt) == SQLITE_ROW) {
            cols++;
        }
        sqlite3_finalize(check_stmt);
        if (cols > 0 && cols < 57) {
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
        "    dirty_full_fallback_predicted_bounds_missing INTEGER,\n"
        "    dirty_full_fallback_composite_missing_input_bounds INTEGER,\n"
        "    dirty_full_fallback_transform_bounds_unknown INTEGER,\n"
        "    dirty_full_fallback_effect_bounds_unknown INTEGER,\n"
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

} // namespace chronon3d::telemetry
#endif
