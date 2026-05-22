#include "sqlite_telemetry_store_impl.hpp"

namespace chronon3d::telemetry {

SqliteTelemetryStore::SqliteTelemetryStore()
    : m_impl(std::make_unique<Impl>()) {}

SqliteTelemetryStore::~SqliteTelemetryStore() = default;

} // namespace chronon3d::telemetry
