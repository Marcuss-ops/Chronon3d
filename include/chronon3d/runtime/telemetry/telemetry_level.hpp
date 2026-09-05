#pragma once

#include <charconv>
#include <string_view>

namespace chronon3d::telemetry {

// ── Telemetry capture levels (TICKET-TELEMETRY-SQLITE-NORMALIZATION §5) ──
//
//   Off       — persistence disabled (manager records nothing).
//   Summary   — durable KPI only: render_runs, render_counters,
//               render_phase_events, render_node_summary,
//               render_memory_summary, render_artifacts.
//               < 1.000 rows/run; this data is the long-term proprietary
//               corpus and is NEVER auto-rotated.
//   Detailed  — adds per-frame and per-event tables (render_frames,
//               render_node_events, render_layer_events, render_cache_events,
//               render_culling_events, render_image_events). High volume;
//               subject to retention TTL.
//   Trace     — adds external trace pointers (Perfetto/Chrome trace
//               artifacts registered as render_artifacts entries).
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

/// Parse the detailed-history retention window in days. 0 disables the janitor
/// (detailed rows are then kept indefinitely, like Summary). Non-numeric or
/// negative input falls back to `fallback`.
[[nodiscard]] inline int parse_retention_days(std::string_view s, int fallback = 30) noexcept {
    if (s.empty()) return fallback;
    int value = -1;
    const auto* begin = s.data();
    const auto* end = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end || value < 0) return fallback;
    return value;
}

} // namespace chronon3d::telemetry
