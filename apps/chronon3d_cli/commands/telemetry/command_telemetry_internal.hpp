#pragma once

#include <cstdint>
#include <string>

// ── Formatting helpers (no SQLite dependency) ────────────────────────────
namespace chronon3d::cli {
std::string format_pct(double value);
std::string format_bytes(uint64_t bytes);
std::string format_ms(double value);
} // namespace chronon3d::cli

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <sqlite3.h>
#include <sstream>

namespace chronon3d::cli {

using RunSummary = chronon3d::telemetry::RenderTelemetryRecord;

// SQL helper declarations
std::string sql_text(sqlite3_stmt* stmt, int col);
int64_t sql_i64(sqlite3_stmt* stmt, int col);
double sql_double(sqlite3_stmt* stmt, int col);
bool prepare_with_run_id(sqlite3* db, sqlite3_stmt** stmt, const char* sql, const std::string& run_id);

// RunSummary query
RunSummary query_run_summary(sqlite3* db, const std::string& run_id);

// Report generation
void generate_telemetry_report(std::stringstream& out, sqlite3* db, const std::string& run_id, const RunSummary& run);

} // namespace chronon3d::cli
