#pragma once

#include "video_export_common.hpp"

#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <memory>
#include <string>

namespace chronon3d::cli {

[[nodiscard]] size_t compute_pipe_arena_size(int width, int height);

[[nodiscard]] FfmpegPipeOptions make_pipe_options(
    const Composition& comp,
    const FfmpegExportOptions& opts,
    const std::string& codec);

[[nodiscard]] bool ensure_output_directory_exists(const std::string& output_path);

void track_pipe_encoder_process(
    const FfmpegExportOptions& opts,
    IVideoEncoder& encoder,
    SystemMetricsCollector& sys_metrics);

void warmup_pipe_renderer(
    SoftwareRenderer& renderer,
    const Composition& comp,
    const FfmpegExportOptions& opts);

[[nodiscard]] double pipe_write_blocked_ms(bool is_native, IVideoEncoder& encoder);

void log_pipe_export_failure(
    bool cancelled,
    bool render_failed,
    bool writer_error,
    bool exception_error);

} // namespace chronon3d::cli
