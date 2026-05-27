#include "sqlite_telemetry_store_impl.hpp"

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>

namespace chronon3d::telemetry {

SqliteTelemetryStore::SqliteTelemetryStore()
    : m_impl(std::make_unique<Impl>()) {}

SqliteTelemetryStore::~SqliteTelemetryStore() = default;

void SqliteTelemetryStore::begin_transaction() {
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    m_impl->mutex.lock();  // held until end_transaction(); write_* methods lock recursively
    if (m_impl->db) {
        exec_sql(m_impl->db, "BEGIN TRANSACTION;");
    }
#endif
}

void SqliteTelemetryStore::end_transaction(bool commit) {
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    if (m_impl->db) {
        if (commit) {
            exec_sql(m_impl->db, "COMMIT;");
        } else {
            exec_sql(m_impl->db, "ROLLBACK;");
        }
    }
    m_impl->mutex.unlock();  // release the lock acquired in begin_transaction()
#endif
}

} // namespace chronon3d::telemetry
#endif
