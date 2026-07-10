#pragma once

#include "video_export_common.hpp"

#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <exception>
#include <memory>
#include <string>

namespace chronon3d::cli {

struct PipeExportStatus {
    bool success{false};   // explicitly set to true only when ALL frames export successfully
    bool cancelled{false};
    bool render_failed{false};
    bool writer_error{false};
    bool exception_error{false};
    int frames_written{0};
};

[[nodiscard]] bool should_log_pipe_progress(int done_count, int total);

[[nodiscard]] int pipe_encoded_frame_count(const PipeExportStatus& status);

void mark_pipe_cancelled(PipeExportStatus& status, Frame frame);
void mark_pipe_writer_failed(PipeExportStatus& status, Frame frame);
void mark_pipe_render_failed(PipeExportStatus& status, Frame frame);
void mark_pipe_exception(PipeExportStatus& status, Frame frame, const std::exception& error);

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
    SoftwareRenderer & renderer,
    const Composition& comp,
    const FfmpegExportOptions& opts);

[[nodiscard]] double pipe_write_blocked_ms(bool is_native, IVideoEncoder& encoder);

void log_pipe_export_failure(
    const PipeExportStatus& status);

/// P1-B: Run ffprobe on the output file to validate it is a valid video
/// with the expected stream, resolution, fps, and duration.
/// Returns true if validation passes, false if ffprobe is missing (with
/// a warning — non-fatal) or if the output is invalid.
[[nodiscard]] bool validate_video_output(
    const std::string& output_path,
    int expected_width,
    int expected_height,
    int expected_fps,
    int64_t expected_frames);

} // namespace chronon3d::cli
