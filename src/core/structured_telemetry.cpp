#include <chronon3d/core/telemetry/structured_telemetry.hpp>
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <sstream>

namespace chronon3d::telemetry {

StructuredTelemetry::StructuredTelemetry() = default;

StructuredTelemetry::~StructuredTelemetry() {
    close();
}

bool StructuredTelemetry::open(const std::filesystem::path& path) {
    if (db_) close();

    if (sqlite3_open(path.string().c_str(), &db_) != SQLITE_OK) {
        spdlog::error("Failed to open telemetry database: {}", sqlite3_errmsg(db_));
        return false;
    }

    return init_db();
}

void StructuredTelemetry::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool StructuredTelemetry::init_db() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS render_runs (
            run_id TEXT PRIMARY KEY,
            composition_id TEXT,
            success INTEGER DEFAULT 1,
            frames_total INTEGER DEFAULT 0,
            frames_written INTEGER DEFAULT 0,
            wall_time_ms REAL DEFAULT 0,
            render_ms REAL DEFAULT 0,
            effective_fps REAL DEFAULT 0,
            output_path TEXT,
            finished_at_iso DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS render_frames (
            run_id TEXT,
            frame_number INTEGER,
            duration_ms REAL,
            memory_mb REAL,
            PRIMARY KEY (run_id, frame_number)
        );

        CREATE TABLE IF NOT EXISTS render_phase_events (
            run_id TEXT,
            frame_number INTEGER,
            phase_name TEXT,
            duration_ms REAL
        );

        CREATE TABLE IF NOT EXISTS render_node_events (
            run_id TEXT,
            frame_number INTEGER,
            node_name TEXT,
            node_type TEXT,
            layer_id TEXT,
            duration_ms REAL,
            cache_status TEXT,
            cache_key_digest TEXT,
            input_count INTEGER,
            output_width INTEGER,
            output_height INTEGER,
            output_bytes INTEGER,
            bbox_x REAL, bbox_y REAL, bbox_w REAL, bbox_h REAL,
            visible_x REAL, visible_y REAL, visible_w REAL, visible_h REAL,
            pixels_touched INTEGER,
            pixels_cleared INTEGER,
            pixels_composited INTEGER,
            pixels_transformed INTEGER,
            pixels_blurred INTEGER,
            PRIMARY KEY (run_id, frame_number, node_name, node_type)
        );

        CREATE TABLE IF NOT EXISTS render_layer_events (
            run_id TEXT,
            frame_number INTEGER,
            layer_id TEXT,
            layer_name TEXT,
            layer_type TEXT,
            duration_ms REAL,
            visible INTEGER,
            cull_reason TEXT,
            opacity REAL,
            blend_mode TEXT,
            bbox_x REAL, bbox_y REAL, bbox_w REAL, bbox_h REAL,
            visible_x REAL, visible_y REAL, visible_w REAL, visible_h REAL,
            area_pixels INTEGER,
            visible_pixels INTEGER,
            dirty_pixels INTEGER,
            effects TEXT,
            effect_padding REAL,
            glyphs_rasterized INTEGER,
            images_sampled INTEGER,
            PRIMARY KEY (run_id, frame_number, layer_id)
        );

        CREATE TABLE IF NOT EXISTS render_cache_events (
            run_id TEXT,
            frame_number INTEGER,
            node_name TEXT,
            cacheable INTEGER,
            cache_status TEXT,
            key_digest TEXT,
            params_hash TEXT,
            source_hash TEXT,
            input_hash TEXT,
            output_bytes INTEGER
        );

        CREATE TABLE IF NOT EXISTS render_counters (
            run_id TEXT,
            frame_number INTEGER,
            counter_name TEXT,
            counter_value INTEGER
        );

        CREATE TABLE IF NOT EXISTS render_text_events (
            run_id TEXT,
            frame_number INTEGER,
            layer_id TEXT,
            text_length INTEGER,
            line_count INTEGER,
            glyph_count INTEGER,
            glyphs_rasterized INTEGER,
            glyph_cache_hits INTEGER,
            glyph_cache_misses INTEGER,
            layout_ms REAL,
            raster_ms REAL,
            composite_ms REAL,
            font_path TEXT,
            font_size REAL
        );

        CREATE TABLE IF NOT EXISTS render_image_events (
            run_id TEXT,
            frame_number INTEGER,
            layer_id TEXT,
            image_path TEXT,
            image_width INTEGER,
            image_height INTEGER,
            cache_status TEXT,
            decode_ms REAL,
            sample_ms REAL,
            sampled_pixels INTEGER
        );

        CREATE TABLE IF NOT EXISTS render_culling_events (
            run_id TEXT,
            frame_number INTEGER,
            layer_id TEXT,
            visible INTEGER,
            reason TEXT,
            bbox_x REAL, bbox_y REAL, bbox_w REAL, bbox_h REAL,
            visible_x REAL, visible_y REAL, visible_w REAL, visible_h REAL,
            saved_pixels INTEGER
        );

        CREATE TABLE IF NOT EXISTS render_tile_events (
            run_id TEXT,
            frame_number INTEGER,
            tile_x INTEGER, tile_y INTEGER, tile_w INTEGER, tile_h INTEGER,
            status TEXT,
            dirty INTEGER,
            dirty_ratio REAL,
            duration_ms REAL,
            key_digest TEXT,
            pixels_copied INTEGER,
            pixels_rendered INTEGER
        );
    )";

    char* errmsg = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        spdlog::error("Failed to initialize telemetry database schema: {}", errmsg);
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

void StructuredTelemetry::record_run(const std::string& run_id, const std::string& comp_id) {
    std::string sql = fmt::format("INSERT OR REPLACE INTO render_runs (run_id, composition_id) VALUES ('{}', '{}');", run_id, comp_id);
    exec_sql(sql);
}

void StructuredTelemetry::record_events(const std::string& run_id, const std::vector<TraceEvent>& events) {
    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    for (const auto& e : events) {
        if (e.category == "Node") {
            record_node_event(run_id, e);
            record_cache_event(run_id, e);
        } else if (e.category == "Layer") {
            record_layer_event(run_id, e);
        } else if (e.category == "frame") {
            std::string sql = fmt::format(
                "INSERT OR REPLACE INTO render_frames (run_id, frame_number, duration_ms) VALUES ('{}', {}, {});",
                run_id, e.frame, e.dur_us / 1000.0
            );
            exec_sql(sql);
        } else {
            // General phase events
            std::string sql = fmt::format(
                "INSERT INTO render_phase_events (run_id, frame_number, phase_name, duration_ms) VALUES ('{}', {}, '{}', {});",
                run_id, e.frame, e.name, e.dur_us / 1000.0
            );
            exec_sql(sql);
        }
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
}

void StructuredTelemetry::record_node_event(const std::string& run_id, const TraceEvent& e) {
    std::string sql = fmt::format(
        "INSERT OR REPLACE INTO render_node_events (run_id, frame_number, node_name, node_type, layer_id, duration_ms, cache_status, cache_key_digest, output_width, output_height, output_bytes, bbox_x, bbox_y, bbox_w, bbox_h, visible_x, visible_y, visible_w, visible_h, pixels_touched, pixels_cleared, pixels_composited, pixels_transformed, pixels_blurred) "
        "VALUES ('{}', {}, '{}', '{}', '{}', {}, '{}', '{}', {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, 0, 0, 0, 0);",
        run_id, e.frame, e.name, e.node_type, e.layer_id, e.dur_us / 1000.0, e.cache_status, e.cache_key_digest,
        0, 0, e.output_bytes, e.bbox_x, e.bbox_y, e.bbox_w, e.bbox_h, e.visible_x, e.visible_y, e.visible_w, e.visible_h, e.pixels_touched
    );
    exec_sql(sql);
}

void StructuredTelemetry::record_layer_event(const std::string& run_id, const TraceEvent& e) {
    std::string sql = fmt::format(
        "INSERT OR REPLACE INTO render_layer_events (run_id, frame_number, layer_id, layer_name, layer_type, duration_ms, visible, cull_reason, opacity, blend_mode, bbox_x, bbox_y, bbox_w, bbox_h, visible_x, visible_y, visible_w, visible_h, area_pixels, visible_pixels, dirty_pixels, effects, effect_padding, glyphs_rasterized, images_sampled) "
        "VALUES ('{}', {}, '{}', '{}', '{}', {}, {}, '', 1.0, 'Normal', {}, {}, {}, {}, {}, {}, {}, {}, 0, 0, 0, '', 0.0, 0, 0);",
        run_id, e.frame, e.layer_id, e.name, e.node_type, e.dur_us / 1000.0, 1, e.bbox_x, e.bbox_y, e.bbox_w, e.bbox_h, e.visible_x, e.visible_y, e.visible_w, e.visible_h
    );
    exec_sql(sql);
}

void StructuredTelemetry::record_cache_event(const std::string& run_id, const TraceEvent& e) {
    std::string sql = fmt::format(
        "INSERT INTO render_cache_events (run_id, frame_number, node_name, cacheable, cache_status, key_digest, output_bytes) "
        "VALUES ('{}', {}, '{}', {}, '{}', '{}', {});",
        run_id, e.frame, e.name, (e.cache_status != "not_cacheable"), e.cache_status, e.cache_key_digest, e.output_bytes
    );
    exec_sql(sql);
}

void StructuredTelemetry::record_counters(const std::string& run_id, int frame, const RenderCounters& c) {
    auto record = [&](const std::string& name, uint64_t val) {
        std::string sql = fmt::format(
            "INSERT INTO render_counters (run_id, frame_number, counter_name, counter_value) VALUES ('{}', {}, '{}', {});",
            run_id, frame, name, val
        );
        exec_sql(sql);
    };

    record("pixels_touched", c.pixels_touched.load());
    record("cache_hits", c.cache_hits.load());
    record("cache_misses", c.cache_misses.load());
    record("nodes_executed", c.nodes_executed.load());
    record("layers_rendered", c.layers_rendered.load());
    record("clear_pixels", c.clear_pixels.load());
    record("composite_pixels", c.composite_pixels.load());
    record("transform_pixels", c.transform_pixels.load());
    record("effect_pixels", c.effect_pixels.load());
    record("text_glyphs_rasterized", c.text_glyphs_rasterized.load());
    record("images_sampled", c.images_sampled.load());

    // Framebuffer pipeline diagnostics
    record("framebuffer_acquire_ms", c.framebuffer_acquire_ms.load());
    record("framebuffer_clear_ms", c.framebuffer_clear_ms.load());
    record("frame_conversion_copy_ms", c.frame_conversion_copy_ms.load());
    record("framebuffer_pool_miss_count_size_mismatch", c.framebuffer_pool_miss_count_size_mismatch.load());
    record("framebuffer_pool_miss_count_empty", c.framebuffer_pool_miss_count_empty.load());
    record("framebuffer_enqueue_ms", c.framebuffer_enqueue_ms.load());
    record("framebuffer_buffer_returned_to_pool_count", c.framebuffer_buffer_returned_to_pool_count.load());
}

void StructuredTelemetry::record_text_event(const std::string& run_id, int frame, const TextEvent& e) {
    std::string sql = fmt::format(
        "INSERT INTO render_text_events (run_id, frame_number, layer_id, text_length, line_count, glyph_count, glyphs_rasterized, glyph_cache_hits, glyph_cache_misses, layout_ms, raster_ms, composite_ms, font_path, font_size) "
        "VALUES ('{}', {}, '{}', {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}', {});",
        run_id, frame, e.layer_id, e.text_length, e.line_count, e.glyph_count, e.glyphs_rasterized, e.glyph_cache_hits, e.glyph_cache_misses, e.layout_ms, e.raster_ms, e.composite_ms, e.font_path, e.font_size
    );
    exec_sql(sql);
}

void StructuredTelemetry::record_image_event(const std::string& run_id, int frame, const ImageEvent& e) {
    std::string sql = fmt::format(
        "INSERT INTO render_image_events (run_id, frame_number, layer_id, image_path, image_width, image_height, cache_status, decode_ms, sample_ms, sampled_pixels) "
        "VALUES ('{}', {}, '{}', '{}', {}, {}, '{}', {}, {}, {});",
        run_id, frame, e.layer_id, e.image_path, e.width, e.height, e.cache_status, e.decode_ms, e.sample_ms, e.sampled_pixels
    );
    exec_sql(sql);
}

void StructuredTelemetry::record_culling_event(const std::string& run_id, int frame, const CullingEvent& e) {
    std::string sql = fmt::format(
        "INSERT INTO render_culling_events (run_id, frame_number, layer_id, visible, reason, bbox_x, bbox_y, bbox_w, bbox_h, visible_x, visible_y, visible_w, visible_h, saved_pixels) "
        "VALUES ('{}', {}, '{}', {}, '{}', {}, {}, {}, {}, {}, {}, {}, {}, {});",
        run_id, frame, e.layer_id, e.visible, e.reason, e.bbox_x, e.bbox_y, e.bbox_w, e.bbox_h, e.visible_x, e.visible_y, e.visible_w, e.visible_h, e.saved_pixels
    );
    exec_sql(sql);
}

void StructuredTelemetry::update_run(const std::string& run_id, const RunUpdate& u) {
    std::string sql = fmt::format(
        "UPDATE render_runs SET success={}, frames_total={}, frames_written={}, wall_time_ms={}, render_ms={}, effective_fps={}, output_path='{}' WHERE run_id='{}';",
        u.success, u.frames_total, u.frames_written, u.wall_time_ms, u.render_ms, u.effective_fps, u.output_path, run_id
    );
    exec_sql(sql);
}

void StructuredTelemetry::exec_sql(const std::string& sql) {
    char* errmsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        spdlog::debug("SQL error: {}", errmsg);
        sqlite3_free(errmsg);
    }
}

} // namespace chronon3d::telemetry
