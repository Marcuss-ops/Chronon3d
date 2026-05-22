#pragma once

#include "render_job.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include <string>
#include <vector>

namespace chronon3d::cli {

/// Writes a single render frame to disk and records telemetry.
bool write_render_frame(const Composition& comp,
                        SoftwareRenderer& renderer,
                        Frame frame,
                        const FrameRange& range,
                        const std::string& output_pattern,
                        bool& ok,
                        std::vector<telemetry::FrameTelemetryRecord>& telemetry_frames,
                        double& total_render_ms,
                        double& total_encode_ms,
                        int& frames_written);

} // namespace chronon3d::cli
