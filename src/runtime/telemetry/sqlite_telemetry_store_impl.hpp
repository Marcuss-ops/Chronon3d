#pragma once

#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>
#include <mutex>

namespace chronon3d::telemetry {

struct SqliteTelemetryStore::Impl {
    sqlite3* db{nullptr};
    std::mutex mutex;

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
