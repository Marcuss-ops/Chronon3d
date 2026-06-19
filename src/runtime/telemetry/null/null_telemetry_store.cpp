#include <chronon3d/runtime/telemetry/null_telemetry_store.hpp>

// NullTelemetryStore is fully inline in the header.
// This TU exists so the build system has a concrete object to compile and link,
// ensuring the null store is always available regardless of telemetry flags.
namespace chronon3d::telemetry {
// (empty — all methods are inline in the header)
} // namespace chronon3d::telemetry
