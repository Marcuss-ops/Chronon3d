#include "../commands.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include <chronon3d/core/structured_telemetry.hpp>
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#endif

namespace chronon3d::cli {

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace {

std::string sql_text(sqlite3_stmt* stmt, int col) {
    const unsigned char* txt = sqlite3_column_text(stmt, col);
    return txt ? reinterpret_cast<const char*>(txt) : "";
}

int64_t sql_i64(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_int64(stmt, col);
}

double sql_double(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_double(stmt, col);
}

std::string format_pct(double value) {
    return fmt::format("{:.1f}%", value * 100.0);
}

std::string format_bytes(uint64_t bytes) {
    return fmt::format("{:.2f} GB", static_cast<double>(bytes) / 1'000'000'000.0);
}

std::string format_ms(double value) {
    return fmt::format("{:.2f} ms", value);
}

bool prepare_with_run_id(sqlite3* db, sqlite3_stmt** stmt, const char* sql, const std::string& run_id) {
    if (sqlite3_prepare_v2(db, sql, -1, stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(*stmt, 1, run_id.c_str(), -1, SQLITE_TRANSIENT);
    return true;
}

} // namespace

int command_telemetry(const TelemetryArgs& args) {
    const std::string db_path = "output/telemetry.db";
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        spdlog::error("Failed to open telemetry database: {}", db_path);
        return 1;
    }

    std::string run_id = args.run_id;
    if (run_id.empty()) {
        const char* last_run_sql = "SELECT run_id FROM render_runs ORDER BY finished_at_iso DESC LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, last_run_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                run_id = sql_text(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }

    if (run_id.empty()) {
        spdlog::error("No render runs found in database.");
        sqlite3_close(db);
        return 1;
    }

    spdlog::info("Generating report for run: {}", run_id);

    struct RunSummary {
        std::string composition_id;
        std::string output_path;
        std::string started_at_iso;
        std::string finished_at_iso;
        std::string git_commit_short;
        std::string build_type;
        std::string compiler_info;
        std::string os;
        std::string cpu_model;
        bool success{false};
        int frames_total{0};
        int frames_written{0};
        int cores{0};
        double wall_time_ms{0.0};
        double render_ms{0.0};
        double encode_ms{0.0};
        double effective_fps{0.0};
        uint64_t pixels_touched{0};
        uint64_t cache_hits{0};
        uint64_t cache_misses{0};
        uint64_t nodes_executed{0};
        uint64_t layers_rendered{0};
        uint64_t text_glyphs_rasterized{0};
        uint64_t images_sampled{0};
        uint64_t blur_pixels{0};
        uint64_t simd_lerp_calls{0};
        uint64_t tiles_total{0};
        uint64_t tiles_hit{0};
        uint64_t tiles_miss{0};
        uint64_t tiles_partial{0};
        uint64_t bytes_allocated_peak{0};
        uint64_t node_cache_hash_collisions{0};
        uint64_t clear_calls{0};
        uint64_t clear_pixels{0};
        uint64_t composite_calls{0};
        uint64_t composite_pixels{0};
        uint64_t transform_calls{0};
        uint64_t transform_pixels{0};
        uint64_t effect_stack_calls{0};
        uint64_t effect_pixels{0};
        uint64_t layer_culling_tests{0};
        uint64_t layers_culled{0};
        uint64_t layers_visible{0};
        uint64_t framebuffer_allocations{0};
        uint64_t framebuffer_reuses{0};
        uint64_t framebuffer_bytes_allocated{0};
        uint64_t framebuffer_bytes_peak{0};
        uint64_t dirty_rect_count{0};
        uint64_t dirty_pixels{0};
        uint64_t dirty_full_fallbacks{0};
    } run;

    {
        const char* run_sql =
            "SELECT composition_id, output_path, success, frames_total, frames_written, wall_time_ms, render_ms, encode_ms, effective_fps, "
            "pixels_touched, cache_hits, cache_misses, nodes_executed, layers_rendered, text_glyphs_rasterized, images_sampled, blur_pixels, "
            "simd_lerp_calls, tiles_total, tiles_hit, tiles_miss, tiles_partial, bytes_allocated_peak, node_cache_hash_collisions, "
            "clear_calls, clear_pixels, composite_calls, composite_pixels, transform_calls, transform_pixels, effect_stack_calls, effect_pixels, "
            "layer_culling_tests, layers_culled, layers_visible, framebuffer_allocations, framebuffer_reuses, framebuffer_bytes_allocated, "
            "framebuffer_bytes_peak, started_at_iso, finished_at_iso, git_commit_short, build_type, compiler_info, os, cpu_model, cores, "
            "dirty_rect_count, dirty_pixels, dirty_full_fallbacks "
            "FROM render_runs WHERE run_id = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, run_sql, run_id) && sqlite3_step(stmt) == SQLITE_ROW) {
            run.composition_id = sql_text(stmt, 0);
            run.output_path = sql_text(stmt, 1);
            run.success = sqlite3_column_int(stmt, 2) != 0;
            run.frames_total = static_cast<int>(sql_i64(stmt, 3));
            run.frames_written = static_cast<int>(sql_i64(stmt, 4));
            run.wall_time_ms = sql_double(stmt, 5);
            run.render_ms = sql_double(stmt, 6);
            run.encode_ms = sql_double(stmt, 7);
            run.effective_fps = sql_double(stmt, 8);
            run.pixels_touched = static_cast<uint64_t>(sql_i64(stmt, 9));
            run.cache_hits = static_cast<uint64_t>(sql_i64(stmt, 10));
            run.cache_misses = static_cast<uint64_t>(sql_i64(stmt, 11));
            run.nodes_executed = static_cast<uint64_t>(sql_i64(stmt, 12));
            run.layers_rendered = static_cast<uint64_t>(sql_i64(stmt, 13));
            run.text_glyphs_rasterized = static_cast<uint64_t>(sql_i64(stmt, 14));
            run.images_sampled = static_cast<uint64_t>(sql_i64(stmt, 15));
            run.blur_pixels = static_cast<uint64_t>(sql_i64(stmt, 16));
            run.simd_lerp_calls = static_cast<uint64_t>(sql_i64(stmt, 17));
            run.tiles_total = static_cast<uint64_t>(sql_i64(stmt, 18));
            run.tiles_hit = static_cast<uint64_t>(sql_i64(stmt, 19));
            run.tiles_miss = static_cast<uint64_t>(sql_i64(stmt, 20));
            run.tiles_partial = static_cast<uint64_t>(sql_i64(stmt, 21));
            run.bytes_allocated_peak = static_cast<uint64_t>(sql_i64(stmt, 22));
            run.node_cache_hash_collisions = static_cast<uint64_t>(sql_i64(stmt, 23));
            run.clear_calls = static_cast<uint64_t>(sql_i64(stmt, 24));
            run.clear_pixels = static_cast<uint64_t>(sql_i64(stmt, 25));
            run.composite_calls = static_cast<uint64_t>(sql_i64(stmt, 26));
            run.composite_pixels = static_cast<uint64_t>(sql_i64(stmt, 27));
            run.transform_calls = static_cast<uint64_t>(sql_i64(stmt, 28));
            run.transform_pixels = static_cast<uint64_t>(sql_i64(stmt, 29));
            run.effect_stack_calls = static_cast<uint64_t>(sql_i64(stmt, 30));
            run.effect_pixels = static_cast<uint64_t>(sql_i64(stmt, 31));
            run.layer_culling_tests = static_cast<uint64_t>(sql_i64(stmt, 32));
            run.layers_culled = static_cast<uint64_t>(sql_i64(stmt, 33));
            run.layers_visible = static_cast<uint64_t>(sql_i64(stmt, 34));
            run.framebuffer_allocations = static_cast<uint64_t>(sql_i64(stmt, 35));
            run.framebuffer_reuses = static_cast<uint64_t>(sql_i64(stmt, 36));
            run.framebuffer_bytes_allocated = static_cast<uint64_t>(sql_i64(stmt, 37));
            run.framebuffer_bytes_peak = static_cast<uint64_t>(sql_i64(stmt, 38));
            run.started_at_iso = sql_text(stmt, 39);
            run.finished_at_iso = sql_text(stmt, 40);
            run.git_commit_short = sql_text(stmt, 41);
            run.build_type = sql_text(stmt, 42);
            run.compiler_info = sql_text(stmt, 43);
            run.os = sql_text(stmt, 44);
            run.cpu_model = sql_text(stmt, 45);
            run.cores = static_cast<int>(sql_i64(stmt, 46));
            run.dirty_rect_count = static_cast<uint64_t>(sql_i64(stmt, 47));
            run.dirty_pixels = static_cast<uint64_t>(sql_i64(stmt, 48));
            run.dirty_full_fallbacks = static_cast<uint64_t>(sql_i64(stmt, 49));
        }
        sqlite3_finalize(stmt);
    }

    const double cache_hit_rate = (run.cache_hits + run.cache_misses) > 0
        ? static_cast<double>(run.cache_hits) / static_cast<double>(run.cache_hits + run.cache_misses)
        : 0.0;
    const double framebuffer_reuse_rate = (run.framebuffer_allocations + run.framebuffer_reuses) > 0
        ? static_cast<double>(run.framebuffer_reuses) / static_cast<double>(run.framebuffer_allocations + run.framebuffer_reuses)
        : 0.0;
    const double dirty_to_touched = run.pixels_touched > 0
        ? static_cast<double>(run.dirty_pixels) / static_cast<double>(run.pixels_touched)
        : 0.0;

    std::stringstream out;
    out << "# Chronon3D Telemetry Report - " << run_id << "\n\n";

    out << "## Overview\n";
    out << "| Field | Value |\n";
    out << "| --- | --- |\n";
    out << "| Composition | " << run.composition_id << " |\n";
    out << "| Run ID | " << run_id << " |\n";
    out << "| Status | " << (run.success ? "SUCCESS" : "FAILED") << " |\n";
    out << "| Finished At | " << run.finished_at_iso << " |\n";
    out << "| Output | " << run.output_path << " |\n";
    out << "| Git Commit | " << run.git_commit_short << " |\n";
    out << "| Build | " << run.build_type << " |\n";
    out << "| OS / CPU | " << run.os << " / " << run.cpu_model << " (" << run.cores << " cores) |\n\n";

    out << "## Performance\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    out << "| Effective FPS | " << fmt::format("{:.2f} fps", run.effective_fps) << " |\n";
    out << "| Wall Duration | " << fmt::format("{:.2f} s", run.wall_time_ms / 1000.0) << " |\n";
    out << "| Render Duration | " << fmt::format("{:.2f} s", run.render_ms / 1000.0) << " |\n";
    out << "| Encode Duration | " << fmt::format("{:.2f} s", run.encode_ms / 1000.0) << " |\n";
    out << "| Peak Memory | " << format_bytes(run.bytes_allocated_peak) << " |\n";
    out << "| Cache Hit Rate | " << format_pct(cache_hit_rate) << " |\n";
    out << "| Frames Total | " << run.frames_total << " |\n";
    out << "| Frames Written | " << run.frames_written << " |\n\n";

    out << "## Frame Summary\n";
    out << "| Metric | Value |\n";
    out << "| --- | --- |\n";
    {
        const char* frame_sql =
            "SELECT COUNT(*), AVG(duration_ms), MIN(duration_ms), MAX(duration_ms), "
            "AVG(COALESCE(cache_hit, 0)), AVG(COALESCE(dirty_area_ratio, 1.0)) "
            "FROM render_frames WHERE run_id = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, frame_sql, run_id) && sqlite3_step(stmt) == SQLITE_ROW) {
            out << "| Frame Count | " << sql_i64(stmt, 0) << " |\n";
            out << "| Average Frame | " << format_ms(sql_double(stmt, 1)) << " |\n";
            out << "| Min Frame | " << format_ms(sql_double(stmt, 2)) << " |\n";
            out << "| Max Frame | " << format_ms(sql_double(stmt, 3)) << " |\n";
            out << "| Frame Cache Hit Rate | " << format_pct(sql_double(stmt, 4)) << " |\n";
            out << "| Average Dirty Ratio | " << format_pct(sql_double(stmt, 5)) << " |\n";
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    out << "## Core Render Phases\n";
    out << "| Phase | Duration |\n";
    out << "| --- | --- |\n";
    {
        const char* phase_sql =
            "SELECT phase_name, SUM(duration_ms) AS total_ms "
            "FROM render_phase_events WHERE run_id = ? "
            "GROUP BY phase_name ORDER BY total_ms DESC;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, phase_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                out << "| " << sql_text(stmt, 0) << " | " << format_ms(sql_double(stmt, 1)) << " |\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    out << "## Telemetry Counters\n";
    out << "| Counter | Value |\n";
    out << "| --- | --- |\n";
    out << "| pixels touched | " << run.pixels_touched << " |\n";
    out << "| cache hits | " << run.cache_hits << " |\n";
    out << "| cache misses | " << run.cache_misses << " |\n";
    out << "| nodes executed | " << run.nodes_executed << " |\n";
    out << "| layers rendered | " << run.layers_rendered << " |\n";
    out << "| text glyphs rasterized | " << run.text_glyphs_rasterized << " |\n";
    out << "| images sampled | " << run.images_sampled << " |\n";
    out << "| blur pixels | " << run.blur_pixels << " |\n";
    out << "| simd lerp calls | " << run.simd_lerp_calls << " |\n";
    out << "| tiles total | " << run.tiles_total << " |\n";
    out << "| tiles hit | " << run.tiles_hit << " |\n";
    out << "| tiles miss | " << run.tiles_miss << " |\n";
    out << "| tiles partial | " << run.tiles_partial << " |\n";
    out << "| node cache hash collisions | " << run.node_cache_hash_collisions << " |\n";
    out << "| clear calls | " << run.clear_calls << " |\n";
    out << "| clear pixels | " << run.clear_pixels << " |\n";
    out << "| composite calls | " << run.composite_calls << " |\n";
    out << "| composite pixels | " << run.composite_pixels << " |\n";
    out << "| transform calls | " << run.transform_calls << " |\n";
    out << "| transform pixels | " << run.transform_pixels << " |\n";
    out << "| effect stack calls | " << run.effect_stack_calls << " |\n";
    out << "| effect pixels | " << run.effect_pixels << " |\n";
    out << "| layer culling tests | " << run.layer_culling_tests << " |\n";
    out << "| layers culled | " << run.layers_culled << " |\n";
    out << "| layers visible | " << run.layers_visible << " |\n";
    out << "| framebuffer allocations | " << run.framebuffer_allocations << " |\n";
    out << "| framebuffer reuses | " << run.framebuffer_reuses << " |\n";
    out << "| framebuffer bytes allocated | " << format_bytes(run.framebuffer_bytes_allocated) << " |\n";
    out << "| framebuffer bytes peak | " << format_bytes(run.framebuffer_bytes_peak) << " |\n";
    out << "| dirty rect count | " << run.dirty_rect_count << " |\n";
    out << "| dirty pixels | " << run.dirty_pixels << " |\n";
    out << "| dirty full fallbacks | " << run.dirty_full_fallbacks << " |\n";
    out << "| framebuffer reuse rate | " << format_pct(framebuffer_reuse_rate) << " |\n\n";

    out << "## Hot Nodes\n";
    out << "| Node | Type | Calls | Total | Avg | Cache Hit Rate | Pixels Touched |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: | ---: |\n";
    {
        const char* node_sql =
            "SELECT node_name, node_type, COUNT(*), SUM(duration_ms), AVG(duration_ms), "
            "SUM(CASE WHEN cache_status='hit' THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN cache_status IN ('hit','miss') THEN 1 ELSE 0 END), "
            "SUM(pixels_touched) "
            "FROM render_node_events WHERE run_id = ? "
            "GROUP BY node_name, node_type ORDER BY SUM(duration_ms) DESC LIMIT 10;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, node_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const double cache_rate = sql_i64(stmt, 6) > 0
                    ? static_cast<double>(sql_i64(stmt, 5)) / static_cast<double>(sql_i64(stmt, 6))
                    : 0.0;
                out << "| " << sql_text(stmt, 0) << " | "
                    << sql_text(stmt, 1) << " | "
                    << sql_i64(stmt, 2) << " | "
                    << format_ms(sql_double(stmt, 3)) << " | "
                    << format_ms(sql_double(stmt, 4)) << " | "
                    << format_pct(cache_rate) << " | "
                    << sql_i64(stmt, 7) << " |\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    out << "## Layer Cost Breakdown\n";
    out << "| Layer | Type | Calls | Time | Visible Pixels | Dirty Pixels | Glyphs | Images |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    {
        const char* layer_sql =
            "SELECT layer_name, layer_type, COUNT(*), SUM(duration_ms), SUM(visible_pixels), SUM(dirty_pixels), "
            "SUM(glyphs_rasterized), SUM(images_sampled) "
            "FROM render_layer_events WHERE run_id = ? "
            "GROUP BY layer_id ORDER BY SUM(duration_ms) DESC LIMIT 10;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, layer_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                out << "| " << sql_text(stmt, 0) << " | "
                    << sql_text(stmt, 1) << " | "
                    << sql_i64(stmt, 2) << " | "
                    << format_ms(sql_double(stmt, 3)) << " | "
                    << sql_i64(stmt, 4) << " | "
                    << sql_i64(stmt, 5) << " | "
                    << sql_i64(stmt, 6) << " | "
                    << sql_i64(stmt, 7) << " |\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    out << "## Frame Samples\n";
    out << "| Frame | Duration | Cache | Dirty Ratio |\n";
    out << "| --- | --- | --- | --- |\n";
    {
        const char* sample_sql =
            "SELECT frame_number, duration_ms, cache_hit, dirty_area_ratio "
            "FROM render_frames WHERE run_id = ? ORDER BY frame_number ASC LIMIT 12;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, sample_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                out << "| #" << sql_i64(stmt, 0) << " | "
                    << format_ms(sql_double(stmt, 1)) << " | "
                    << (sqlite3_column_int(stmt, 2) ? "hit" : "miss") << " | "
                    << format_pct(sql_double(stmt, 3)) << " |\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    out << "## Cache Diagnostics\n";
    {
        const char* cache_sql =
            "SELECT cache_status, COUNT(*) FROM render_cache_events WHERE run_id = ? "
            "GROUP BY cache_status ORDER BY COUNT(*) DESC;";
        sqlite3_stmt* stmt = nullptr;
        if (prepare_with_run_id(db, &stmt, cache_sql, run_id)) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                out << "- " << sql_text(stmt, 0) << ": " << sql_i64(stmt, 1) << "\n";
            }
        }
        sqlite3_finalize(stmt);
    }
    out << "\n";

    out << "## Things to Know\n";
    out << "- Cache hit rate: " << format_pct(cache_hit_rate) << ".\n";
    out << "- Average dirty coverage: " << format_pct(dirty_to_touched) << " of touched pixels.\n";
    out << "- Dirty full fallbacks: " << run.dirty_full_fallbacks << ".\n";
    out << "- Framebuffer reuse rate: " << format_pct(framebuffer_reuse_rate) << ".\n";
    out << "- If render time stays high while cache hit rate is strong, the hot path is likely compositing, clear passes, or framebuffer churn rather than rasterization.\n";
    out << "- If text glyph rasterization is low, text is probably not the main bottleneck anymore; blur/glow and layer recomposition become the next suspects.\n";
    out << "\n";

    const std::string output_path = args.output_file.empty() ? "output/telemetry_report.md" : args.output_file;
    std::ofstream f(output_path);
    f << out.str();
    spdlog::info("Report saved to: {}", output_path);

    sqlite3_close(db);
    return 0;
}
#else
int command_telemetry(const TelemetryArgs& args) {
    (void)args;
    return 0;
}
#endif

} // namespace chronon3d::cli
