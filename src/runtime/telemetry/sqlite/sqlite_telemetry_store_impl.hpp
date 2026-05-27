#pragma once

#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <utility>

namespace chronon3d::telemetry {

class SqliteStatement {
public:
    SqliteStatement() = default;
    SqliteStatement(sqlite3* db, const char* sql) { prepare(db, sql); }
    ~SqliteStatement() { finalize(); }

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    SqliteStatement(SqliteStatement&& other) noexcept
        : stmt_(std::exchange(other.stmt_, nullptr)) {}

    SqliteStatement& operator=(SqliteStatement&& other) noexcept {
        if (this != &other) {
            finalize();
            stmt_ = std::exchange(other.stmt_, nullptr);
        }
        return *this;
    }

    bool prepare(sqlite3* db, const char* sql) {
        finalize();
        return sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) == SQLITE_OK;
    }

    explicit operator bool() const { return stmt_ != nullptr; }

    sqlite3_stmt* get() const { return stmt_; }

    void finalize() {
        if (stmt_) {
            sqlite3_finalize(stmt_);
            stmt_ = nullptr;
        }
    }

    bool reset() { return stmt_ && sqlite3_reset(stmt_) == SQLITE_OK; }
    bool step_done() { return stmt_ && sqlite3_step(stmt_) == SQLITE_DONE; }
    bool step_row() { return stmt_ && sqlite3_step(stmt_) == SQLITE_ROW; }
    bool clear_bindings() { return stmt_ && sqlite3_clear_bindings(stmt_) == SQLITE_OK; }

    bool bind(int index, std::string_view value) {
        return stmt_ && sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }

    bool bind(int index, const std::string& value) {
        return bind(index, std::string_view(value));
    }

    bool bind(int index, const char* value) {
        return stmt_ && sqlite3_bind_text(stmt_, index, value ? value : "", -1, SQLITE_TRANSIENT) == SQLITE_OK;
    }

    bool bind(int index, bool value) {
        return stmt_ && sqlite3_bind_int(stmt_, index, value ? 1 : 0) == SQLITE_OK;
    }

    template <typename T>
    requires (std::is_integral_v<T> && !std::is_same_v<std::remove_cv_t<T>, bool>)
    bool bind(int index, T value) {
        if constexpr (sizeof(T) <= sizeof(int) && std::is_signed_v<T>) {
            return stmt_ && sqlite3_bind_int(stmt_, index, static_cast<int>(value)) == SQLITE_OK;
        } else {
            return stmt_ && sqlite3_bind_int64(stmt_, index, static_cast<sqlite3_int64>(value)) == SQLITE_OK;
        }
    }

    template <typename T>
    requires std::is_floating_point_v<T>
    bool bind(int index, T value) {
        return stmt_ && sqlite3_bind_double(stmt_, index, static_cast<double>(value)) == SQLITE_OK;
    }

private:
    sqlite3_stmt* stmt_{nullptr};
};

template <typename... Args>
bool bind_all(SqliteStatement& stmt, Args&&... args) {
    int index = 1;
    return (... && stmt.bind(index++, std::forward<Args>(args)));
}

inline bool exec_sql(sqlite3* db, const char* sql) {
    char* err_msg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        return false;
    }
    return true;
}

struct SqliteTelemetryStore::Impl {
    sqlite3* db{nullptr};
    std::recursive_mutex mutex;

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

} // namespace chronon3d::telemetry

#else

namespace chronon3d::telemetry {

struct SqliteTelemetryStore::Impl {};

} // namespace chronon3d::telemetry

#endif
