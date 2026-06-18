#include "sqlite_telemetry_store_impl.hpp"
#include "telemetry_schema.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace chronon3d::telemetry {

namespace {

// Canonical column count for render_runs (from telemetry_schema.sql)
constexpr int CANONICAL_RUN_COLUMNS = 120;

// Ordered column names for render_runs matching telemetry_schema.sql
constexpr const char* RUN_COLUMN_NAMES[] = {
    "run_id", "composition_id", "output_path", "success", "error_code", "error_message",
    "frames_total", "frames_written", "wall_time_ms", "render_ms", "encode_ms",
    "effective_fps", "pixels_touched", "cache_hits", "cache_misses", "nodes_executed",
    "layers_rendered", "text_glyphs_rasterized", "images_sampled", "blur_pixels",
    "simd_lerp_calls",
    "bytes_allocated_peak", "node_cache_hash_collisions",
    "clear_skipped_calls", "clear_skipped_pixels", "clear_calls", "clear_pixels", "composite_calls", "composite_pixels",
    "transform_calls", "transform_pixels", "effect_stack_calls", "effect_pixels",
    "layer_culling_tests", "layers_culled", "layers_visible",
    "framebuffer_allocations", "framebuffer_reuses", "framebuffer_bytes_allocated",
    "framebuffer_bytes_peak",
    "dirty_rect_count", "dirty_pixels", "dirty_union_area_pixels", "dirty_full_fallbacks",
    "bypass_not_cacheable_count",
    "dirty_full_fallback_predicted_bounds_missing",
    "dirty_full_fallback_composite_missing_input_bounds",
    "dirty_full_fallback_transform_bounds_unknown",
    "dirty_full_fallback_effect_bounds_unknown",
    "framebuffer_acquire_ms", "framebuffer_clear_ms",    "clearnode_ms",
    "clearnode_restore_ms",
    "clearnode_restore_rect_count", "clearnode_restore_pixels", "clearnode_restore_bytes",
    "clearnode_restore_full_frame_count", "clearnode_restore_dirty_rect_count", "clearnode_restore_noop_count",
    "framebuffer_pool_clear_ms", "framebuffer_enqueue_ms",
    "framebuffer_pool_empty_alloc", "framebuffer_pool_miss_count_empty",
    "framebuffer_pool_best_fit_reuse", "framebuffer_pool_exact_hit",    "framebuffer_buffer_returned_to_pool_count",
    "framebuffer_pool_budget_bytes",
    "framebuffer_pool_retained_bytes",
    "framebuffer_pool_evicted_count",
    "framebuffer_pool_evicted_bytes",
    "framebuffer_pool_pressure_count",
    "framebuffer_pool_size_class_count",
    "unaligned_memory_copies", "frame_conversion_copy_ms",
    "video_graph_eval_ms", "video_conversion_ms",
    "video_pipe_write_ms", "video_ffmpeg_latency_ms",
    "io_queue_push_blocked_ms", "io_queue_pop_wait_ms", "io_writer_idle_wait_ms",
    "io_queue_peak_depth", "ffmpeg_pipe_write_blocked_ms", "converted_frame_cache_hits", "ffmpeg_flush_ms",
    "io_queue_peak_bytes", "setup_graph_parsing_ms", "setup_asset_io_load_ms",
    "setup_pool_preallocation_ms", "image_decode_ms",
    "compiled_graph_refresh_ms", "cache_eval_ms", "dirty_eval_ms",    "input_resolve_ms",
    "predicted_bbox_ms", "clone_context_ms", "state_assign_ms",
    "framebuffer_lifetime_ms", "node_schedule_ms", "node_dispatch_ms",
    "node_execute_actual_ms", "node_overhead_ms", "telemetry_emit_ms",
    "chronon_render_only_ms", "chronon_conversion_copy_ms", "chronon_queue_wait_ms",
    "chronon_render_throughput_ms", "ffmpeg_encode_total_ms", "ffmpeg_flush_close_ms",
    "e2e_wall_ms",
    "started_at_iso", "finished_at_iso", "git_commit_short", "build_type",
    "compiler_info", "os", "cpu_model", "cores",
    "image_sample_ms", "image_sampled_pixels"
};

// Map of table name → expected column names (for migration across all tables)
struct TableDef {
    const char* name;
    const char* const* columns;
    size_t column_count;
};

#define COLUMNS_OF(arr) (arr), (sizeof(arr) / sizeof((arr)[0]))

// Column definitions for all tables
constexpr const char* FRAME_COL_NAMES[] = {
    "run_id", "frame_number", "duration_ms", "cache_hit", "dirty_area_ratio",
    "graph_eval_ms", "queue_wait_ms", "conversion_copy_ms", "encoder_ms", "pipe_write_ms",
    "native_convert_ms", "native_send_ms", "native_receive_ms", "native_mux_ms",
    "dirty_rect_enabled", "dirty_rect_x0", "dirty_rect_y0", "dirty_rect_x1", "dirty_rect_y1",
    "tile_execution_used", "fast_path_reused", "graph_reused"
};
constexpr const char* PHASE_COL_NAMES[] = {"run_id", "phase_name", "duration_ms"};
constexpr const char* COUNTER_COL_NAMES[] = {"run_id", "counter_name", "counter_value"};
constexpr const char* NODE_EVENT_COL_NAMES[] = {
    "run_id", "frame_number", "node_name", "node_type", "layer_id", "duration_ms",
    "cache_status", "cache_key_digest", "input_count", "output_width", "output_height",
    "output_bytes", "bbox_x", "bbox_y", "bbox_w", "bbox_h",
    "visible_x", "visible_y", "visible_w", "visible_h",
    "pixels_touched", "pixels_cleared", "pixels_composited", "pixels_transformed", "pixels_blurred"
};
constexpr const char* LAYER_EVENT_COL_NAMES[] = {
    "run_id", "frame_number", "layer_id", "layer_name", "layer_type", "duration_ms",
    "visible", "cull_reason", "opacity", "blend_mode",
    "bbox_x", "bbox_y", "bbox_w", "bbox_h",
    "visible_x", "visible_y", "visible_w", "visible_h",
    "area_pixels", "visible_pixels", "dirty_pixels", "effects",
    "effect_padding", "glyphs_rasterized", "images_sampled"
};
constexpr const char* CACHE_EVENT_COL_NAMES[] = {
    "run_id", "frame_number", "node_name", "cacheable", "cache_status",
    "key_digest", "params_hash", "source_hash", "input_hash", "output_bytes",
    "key_width", "key_height", "key_frame", "key_tile_x", "key_tile_y",
    "key_tile_size", "key_tile_hash"
};
constexpr const char* CULLING_EVENT_COL_NAMES[] = {
    "run_id", "frame_number", "layer_id", "visible", "reason",
    "bbox_x", "bbox_y", "bbox_w", "bbox_h",
    "visible_x", "visible_y", "visible_w", "visible_h", "saved_pixels"
};
constexpr const char* TEXT_EVENT_COL_NAMES[] = {
    "run_id", "frame_number", "layer_id",
    "text_length", "line_count", "glyph_count", "glyphs_rasterized",
    "glyph_cache_hits", "glyph_cache_misses",
    "layout_ms", "raster_ms", "composite_ms", "font_path", "font_size"
};
constexpr const char* IMAGE_EVENT_COL_NAMES[] = {
    "run_id", "frame_number", "layer_id",
    "image_path", "image_width", "image_height", "cache_status",
    "decode_ms", "sample_ms", "sampled_pixels"
};
constexpr const char* TILE_EVENT_COL_NAMES[] = {
    "run_id", "frame_number", "layer_id", "tile_x", "tile_y",
    "tile_status", "dirty_rects_count"
};

constexpr TableDef ALL_TABLES[] = {
    {"render_runs", COLUMNS_OF(RUN_COLUMN_NAMES)},
    {"render_frames", COLUMNS_OF(FRAME_COL_NAMES)},
    {"render_phase_events", COLUMNS_OF(PHASE_COL_NAMES)},
    {"render_counters", COLUMNS_OF(COUNTER_COL_NAMES)},
    {"render_node_events", COLUMNS_OF(NODE_EVENT_COL_NAMES)},
    {"render_layer_events", COLUMNS_OF(LAYER_EVENT_COL_NAMES)},
    {"render_cache_events", COLUMNS_OF(CACHE_EVENT_COL_NAMES)},
    {"render_culling_events", COLUMNS_OF(CULLING_EVENT_COL_NAMES)},
    {"render_text_events", COLUMNS_OF(TEXT_EVENT_COL_NAMES)},
    {"render_image_events", COLUMNS_OF(IMAGE_EVENT_COL_NAMES)},
    {"render_tile_events", COLUMNS_OF(TILE_EVENT_COL_NAMES)},
};

#undef COLUMNS_OF

// Non-destructive migration: add missing columns to ALL tables with ALTER TABLE ADD COLUMN
void migrate_add_missing_columns(sqlite3* db) {
    for (const auto& table_def : ALL_TABLES) {
        std::string pragma_sql = "PRAGMA table_info(" + std::string(table_def.name) + ");";
        SqliteStatement check_stmt(db, pragma_sql.c_str());
        if (!check_stmt) {
            continue;
        }

        // Collect existing column names for this table
        std::vector<std::string> existing;
        while (check_stmt.step_row()) {
            const char* col_name = reinterpret_cast<const char*>(sqlite3_column_text(check_stmt.get(), 1));
            if (col_name) existing.push_back(col_name);
        }

        if (existing.empty()) continue; // Table doesn't exist yet, CREATE will handle it

        // Add any missing columns
        for (size_t i = 0; i < table_def.column_count; ++i) {
            bool found = false;
            for (const auto& ex : existing) {
                if (ex == table_def.columns[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::string alter_sql = "ALTER TABLE " + std::string(table_def.name)
                    + " ADD COLUMN " + std::string(table_def.columns[i]) + " INTEGER DEFAULT 0;";
                exec_sql(db, alter_sql.c_str());
            }
        }
    }
}

} // namespace

bool SqliteTelemetryStore::initialize(const std::string& db_path) {
    std::scoped_lock lock(m_impl->mutex);
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

    sqlite3_busy_timeout(m_impl->db, 5000);

    // Set performance PRAGMAs
    exec_sql(m_impl->db, "PRAGMA journal_mode=WAL;");
    exec_sql(m_impl->db, "PRAGMA synchronous=NORMAL;");

    // Non-destructive migration: add any missing columns to existing tables
    migrate_add_missing_columns(m_impl->db);

    // Apply schema from the canonical .sql file (embedded at build time)
    char* err_msg = nullptr;
    rc = sqlite3_exec(m_impl->db, TELEMETRY_SCHEMA_SQL, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }
    return true;
}

} // namespace chronon3d::telemetry
