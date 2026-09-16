#pragma once

#include "video_export_common.hpp"

#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/render_preparation.hpp>

#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace chronon3d::cli {

/// Staging path for an output that is published only after the container is
/// final (and, where a contract exists, verified): the write goes here and the
/// publish step is a single atomic rename.
///
/// AUDIT §5 — one authority. This was spelled three times with two different
/// rules (bare `.partial` suffix, or `.partial` inserted before the extension
/// with `.mp4` defaulted in), so an output path without an extension produced
/// `name.partial` in one lane and `name.partial.mp4` in another — while both
/// lanes fed the same rename and the same verifier.
[[nodiscard]] std::filesystem::path make_partial_output_path(
    const std::filesystem::path& final_path);

struct PipeExportStatus {
    bool success{false};   // explicitly set to true only when ALL frames export successfully
    bool cancelled{false};
    bool render_failed{false};
    bool writer_error{false};
    bool exception_error{false};
    int frames_rendered{0};
    int frames_enqueued{0};
    int frames_encoded{0};
};

[[nodiscard]] bool should_log_pipe_progress(int done_count, int total);

[[nodiscard]] int pipe_encoded_frame_count(const PipeExportStatus& status);

void mark_pipe_cancelled(PipeExportStatus& status, Frame frame);
void mark_pipe_writer_failed(PipeExportStatus& status, Frame frame);
void mark_pipe_render_failed(PipeExportStatus& status, Frame frame);
void mark_pipe_exception(PipeExportStatus& status, Frame frame, const std::exception& error);

[[nodiscard]] size_t compute_pipe_arena_size(int width, int height);

/// Build the encoder pipe options from the export options.
///
/// Returns `std::nullopt` (after logging) when a requested pipe pixel format
/// is outside the canonical vocabulary. There is no silent fallback: an
/// unknown format must fail the export, not quietly become RGBA.
[[nodiscard]] std::optional<FfmpegPipeOptions> make_pipe_options(
    const CompiledComposition& compiled,
    const FfmpegExportOptions& opts,
    const std::string& codec,
    const chronon3d::CpuBudget& cpu_budget);

[[nodiscard]] bool ensure_output_directory_exists(const std::string& output_path);

void track_pipe_encoder_process(
    const FfmpegExportOptions& opts,
    IVideoEncoder& encoder,
    SystemMetricsCollector& sys_metrics);

[[nodiscard]] chronon3d::Result<chronon3d::runtime::RendererWarmupResult,
                                chronon3d::runtime::PreparationError>
warmup_pipe_renderer(
    SoftwareRenderer & renderer,
    const CompiledComposition& compiled,
    const FfmpegExportOptions& opts,
    chronon3d::runtime::RenderPreparationTimings* out_timings = nullptr);

[[nodiscard]] double pipe_write_blocked_ms(bool is_native, IVideoEncoder& encoder);

void log_pipe_export_failure(
    const PipeExportStatus& status);

} // namespace chronon3d::cli
