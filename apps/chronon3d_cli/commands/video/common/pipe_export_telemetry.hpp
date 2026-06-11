#pragma once

#include "pipe_export_session.hpp"
#include "pipe_export_close.hpp"
#include "pipe_export_setup.hpp"

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <string>
#include <vector>

namespace chronon3d::cli {

/// Build telemetry phase records from benchmark data and pipeline counters.
[[nodiscard]] std::vector<chronon3d::telemetry::PhaseTelemetryRecord>
    build_pipe_export_phases(
        const PipeExportBenchmarkData& bench,
        const RenderCounters* counters,
        bool is_native);

/// Enrich counters with pipe-export-specific metrics.
[[nodiscard]] std::vector<chronon3d::telemetry::CounterTelemetryRecord>
    build_pipe_export_counters(
        const RenderCounters& counters,
        SoftwareRenderer* sw_renderer,
        double write_blocked_ms,
        double queue_wait_ms);

/// Merge frame-encoder telemetry records into frame telemetry (by frame_number).
void merge_encoder_telemetry(
    std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
    std::vector<FrameEncoderTelemetryRecord>& frame_encoder_telemetry);

/// Collect all telemetry and record the output run (SQLite + JSON).
void record_pipe_export_run(
    const std::string& composition_id,
    const std::string& output_path,
    const PipeExportStatus& status,
    int total_frames,
    const PipeExportBenchmarkData& bench,
    const std::string& started_at_iso,
    const std::vector<chronon3d::telemetry::PhaseTelemetryRecord>& phases,
    const std::vector<chronon3d::telemetry::CounterTelemetryRecord>& counters,
    const chronon3d::telemetry::TelemetryBundle& telemetry,
    const RenderCounters* counters_src);

} // namespace chronon3d::cli
