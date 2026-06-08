#pragma once

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::cli {

/// Context needed to generate a run execution report.
struct RenderReportContext {
    chronon3d::telemetry::RenderTelemetryRecord run;
    std::vector<chronon3d::telemetry::CounterTelemetryRecord> counters;
    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases;
    std::vector<chronon3d::telemetry::FrameTelemetryRecord> frames;
    uint64_t pool_current_bytes{0};
    uint64_t pool_available_count{0};
    std::string command_line;
};

/// Generate the execution report and write it to a log file.
/// Returns the path to the generated report file (empty on failure).
std::string generate_execution_report(const RenderReportContext& ctx);

} // namespace chronon3d::cli
