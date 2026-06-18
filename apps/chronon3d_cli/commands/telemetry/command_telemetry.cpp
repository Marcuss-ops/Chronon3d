#include "command_telemetry_internal.hpp"
#include "../../commands.hpp"
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace chronon3d::cli {

int command_telemetry(const TelemetryArgs& args) {
    const std::string db_path = chronon3d::telemetry::TelemetryManager::resolve_sqlite_telemetry_path().string();
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

    auto run = query_run_summary(db, run_id);

    std::stringstream out;
    generate_telemetry_report(out, db, run_id, run);

    const std::string output_path = args.output_file.empty() ? "output/telemetry_report.md" : args.output_file;
    std::ofstream f(output_path);
    f << out.str();
    spdlog::info("Report saved to: {}", output_path);

    sqlite3_close(db);
    return 0;
}

} // namespace chronon3d::cli
