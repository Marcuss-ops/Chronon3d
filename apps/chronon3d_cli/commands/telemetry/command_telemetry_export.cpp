#include "command_telemetry_internal.hpp"
#include "../../commands.hpp"
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <nlohmann/json.hpp>
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>
#endif
#include <spdlog/spdlog.h>
#include <fstream>
#include <iostream>
#include <string>

namespace chronon3d::cli {

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY

namespace {

/// Resolves the requested run id (explicit, or the latest finished run).
std::string resolve_export_run_id(sqlite3* db, const std::string& requested) {
    if (!requested.empty()) {
        return requested;
    }
    const char* last_run_sql = "SELECT run_id FROM render_runs ORDER BY finished_at_iso DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    std::string run_id;
    if (sqlite3_prepare_v2(db, last_run_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            run_id = sql_text(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return run_id;
}

/// Emits one JSONL record per run, keyed by the render_runs column names so
/// the export never drifts from the SQLite schema.  This is the explicit
/// compatibility export (SQLite -> JSONL); nothing writes JSONL during a
/// render anymore.
void write_run_row_jsonl(sqlite3* db, const std::string& run_id, std::ostream& out) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT * FROM render_runs WHERE run_id = ?;";
    if (!prepare_with_run_id(db, &stmt, sql, run_id)) {
        return;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return;
    }

    nlohmann::json record;
    record["type"] = "run";
    record["run_id"] = run_id;

    const int col_count = sqlite3_column_count(stmt);
    for (int i = 0; i < col_count; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        if (!name) {
            continue;
        }
        switch (sqlite3_column_type(stmt, i)) {
        case SQLITE_INTEGER:
            record[name] = sqlite3_column_int64(stmt, i);
            break;
        case SQLITE_FLOAT:
            record[name] = sqlite3_column_double(stmt, i);
            break;
        case SQLITE_TEXT:
            record[name] = sql_text(stmt, i);
            break;
        case SQLITE_NULL:
        default:
            record[name] = nullptr;
            break;
        }
    }
    sqlite3_finalize(stmt);

    out << record.dump() << "\n";
}

} // namespace

#endif // CHRONON3D_ENABLE_SQLITE_TELEMETRY

int command_telemetry_export(const TelemetryExportArgs& args) {
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    const std::string db_path = chronon3d::telemetry::TelemetryManager::resolve_sqlite_telemetry_path().string();
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        spdlog::error("Failed to open telemetry database: {}", db_path);
        return 1;
    }

    const std::string run_id = resolve_export_run_id(db, args.run_id);
    if (run_id.empty()) {
        spdlog::error("No render runs found in database.");
        sqlite3_close(db);
        return 1;
    }

    if (args.output_file.empty()) {
        write_run_row_jsonl(db, run_id, std::cout);
    } else {
        std::ofstream out(args.output_file);
        if (!out.is_open()) {
            spdlog::error("Failed to open output file: {}", args.output_file);
            sqlite3_close(db);
            return 1;
        }
        write_run_row_jsonl(db, run_id, out);
        spdlog::info("JSONL export saved to: {}", args.output_file);
    }

    sqlite3_close(db);
    return 0;
#else
    (void)args;
    spdlog::info("Telemetry support is disabled in this build.");
    return 0;
#endif
}

} // namespace chronon3d::cli
