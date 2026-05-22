#include "command_telemetry_internal.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
#include <sqlite3.h>
#include <fmt/format.h>
#include <string>
#include <cstdint>

namespace chronon3d::cli {

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

} // namespace chronon3d::cli
#endif
