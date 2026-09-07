#pragma once

#include <charconv>
#include <string_view>

namespace chronon3d::telemetry {

// ── Telemetry capture levels ─────────────────────────────────────────────
//
//   Off       — capture disabled (manager records nothing).
//   Summary   — KPI-level events only (default; small bounded volume).
//   Detailed  — per-frame/per-event detail events. High volume.
//   Trace     — adds external trace pointers (Perfetto/Chrome trace
//               artifacts registered as render_artifacts entries).
//
// Levels gate in-memory event capture only: Chronon emits events and owns
// no persistence (Wave 1D). A host-registered TelemetryStore decides what
// to do with the drained records.
enum class TelemetryLevel : unsigned char {
    Off = 0,
    Summary = 1,
    Detailed = 2,
    Trace = 3,
};

inline constexpr TelemetryLevel kDefaultTelemetryLevel = TelemetryLevel::Summary;

[[nodiscard]] inline constexpr std::string_view telemetry_level_name(TelemetryLevel level) noexcept {
    switch (level) {
    case TelemetryLevel::Off:      return "off";
    case TelemetryLevel::Summary:  return "summary";
    case TelemetryLevel::Detailed: return "detailed";
    case TelemetryLevel::Trace:    return "trace";
    }
    return "summary";
}

/// Parse the boundary-resolved level string (Config/CLI startup owns getenv;
/// this function never touches the environment). Unknown/empty values fall
/// back to the production-safe default (Summary), never to a noisier level.
[[nodiscard]] inline TelemetryLevel parse_telemetry_level(std::string_view s) noexcept {
    if (s == "off" || s == "0")        return TelemetryLevel::Off;
    if (s == "detailed" || s == "2")   return TelemetryLevel::Detailed;
    if (s == "trace" || s == "3")      return TelemetryLevel::Trace;
    return TelemetryLevel::Summary;  // "summary", "1", empty, unknown
}

} // namespace chronon3d::telemetry
