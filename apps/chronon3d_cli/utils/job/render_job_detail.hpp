#pragma once

#include "render_job.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include <memory>
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

/// Writes a pre-rendered framebuffer to disk (used for double-buffered pipeline).
/// Returns the encode time in ms, or -1 on failure.
/// Updates telemetry_frames, total_encode_ms, and frames_written.
double write_frame_to_disk(std::shared_ptr<Framebuffer> fb,
                           Frame frame,
                           const FrameRange& range,
                           const std::string& output_pattern,
                           bool cache_hit,
                           double dirty_ratio,
                           double render_ms,
                           int program_cache_capacity,
                           bool& ok,
                           std::vector<telemetry::FrameTelemetryRecord>& telemetry_frames,
                           double& total_encode_ms,
                           int& frames_written);

} // namespace chronon3d::cli
