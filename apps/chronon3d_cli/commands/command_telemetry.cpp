#include "../commands.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include <chronon3d/core/structured_telemetry.hpp>
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <fstream>
#include <vector>
#include <map>
#endif

namespace chronon3d::cli {

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
int command_telemetry(const TelemetryArgs& args) {
    std::string db_path = "output/telemetry.db";
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        spdlog::error("Failed to open telemetry database: {}", db_path);
        return 1;
    }

    std::string run_id = args.run_id;
    if (run_id.empty()) {
        // Find last run
        const char* last_run_sql = "SELECT run_id FROM render_runs ORDER BY finished_at_iso DESC LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, last_run_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                run_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
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

    std::stringstream out;
    out << "# Chronon3D Telemetry Report - " << run_id << "\n\n";

    // 1. Run Summary
    std::string run_sql = fmt::format("SELECT composition_id, wall_time_ms, render_ms, effective_fps, output_path FROM render_runs WHERE run_id = '{}';", run_id);
    sqlite3_stmt* run_stmt = nullptr;
    if (sqlite3_prepare_v2(db, run_sql.c_str(), -1, &run_stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(run_stmt) == SQLITE_ROW) {
            out << "## Performance Metrics\n";
            out << "- Composition: " << sqlite3_column_text(run_stmt, 0) << "\n";
            out << "- Wall Duration: " << sqlite3_column_double(run_stmt, 1) / 1000.0 << " s\n";
            out << "- Render Duration: " << sqlite3_column_double(run_stmt, 2) / 1000.0 << " s\n";
            out << "- Effective FPS: " << sqlite3_column_double(run_stmt, 3) << " fps\n";
            out << "- Output Path: " << sqlite3_column_text(run_stmt, 4) << "\n\n";
        }
        sqlite3_finalize(run_stmt);
    }

    // 2. Core Bottlenecks (Phases)
    out << "## Core Render Phases\n";
    out << "| Phase | Time | Note |\n";
    out << "|---|---:|---|\n";
    std::string phase_sql = fmt::format("SELECT phase_name, SUM(duration_ms) as total_ms FROM render_phase_events WHERE run_id = '{}' GROUP BY phase_name ORDER BY total_ms DESC;", run_id);
    sqlite3_stmt* phase_stmt = nullptr;
    if (sqlite3_prepare_v2(db, phase_sql.c_str(), -1, &phase_stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(phase_stmt) == SQLITE_ROW) {
            out << "| " << sqlite3_column_text(phase_stmt, 0) << " | " 
                << fmt::format("{:.2f} ms", sqlite3_column_double(phase_stmt, 1)) << " | |\n";
        }
        sqlite3_finalize(phase_stmt);
    }
    out << "\n";

    // 3. Layer Breakdown
    out << "## Layer Cost Breakdown\n";
    out << "| Layer | Type | Time | Pixels | Cache | Effects |\n";
    out << "|---|---|---:|---:|---|---|\n";
    std::string layer_sql = fmt::format(
        "SELECT layer_name, layer_type, SUM(duration_ms) as total_ms, SUM(visible_pixels) as pixels, GROUP_CONCAT(DISTINCT effects) "
        "FROM render_layer_events WHERE run_id = '{}' GROUP BY layer_id ORDER BY total_ms DESC;", run_id);
    sqlite3_stmt* layer_stmt = nullptr;
    if (sqlite3_prepare_v2(db, layer_sql.c_str(), -1, &layer_stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(layer_stmt) == SQLITE_ROW) {
            out << "| " << sqlite3_column_text(layer_stmt, 0) << " | "
                << sqlite3_column_text(layer_stmt, 1) << " | "
                << fmt::format("{:.2f} ms", sqlite3_column_double(layer_stmt, 2)) << " | "
                << sqlite3_column_int(layer_stmt, 3) << " | miss | "
                << (sqlite3_column_text(layer_stmt, 4) ? reinterpret_cast<const char*>(sqlite3_column_text(layer_stmt, 4)) : "none") << " |\n";
        }
        sqlite3_finalize(layer_stmt);
    }
    out << "\n";

    // 4. Cache Diagnostics
    out << "## Cache Diagnostics\n";
    std::string cache_sql = fmt::format(
        "SELECT cache_status, COUNT(*) FROM render_cache_events WHERE run_id = '{}' GROUP BY cache_status;", run_id);
    sqlite3_stmt* cache_stmt = nullptr;
    if (sqlite3_prepare_v2(db, cache_sql.c_str(), -1, &cache_stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(cache_stmt) == SQLITE_ROW) {
            out << "- " << sqlite3_column_text(cache_stmt, 0) << ": " << sqlite3_column_int(cache_stmt, 1) << "\n";
        }
        sqlite3_finalize(cache_stmt);
    }
    out << "\n";

    std::string output_path = args.output_file.empty() ? "output/telemetry_report.md" : args.output_file;
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
