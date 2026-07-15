#pragma once

#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/timeline/render_job.hpp>

#include "../../commands/video/common/encoder_options.hpp"
#include "../../commands/video/common/output_options.hpp"
#include "../../commands/video/common/pipe_options.hpp"
#include "../../commands/video/common/sink_options.hpp"
#include "../../commands/video/common/video_export_common.hpp"
#include "../../commands/video/common/warmup_options.hpp"

#include <optional>
#include <string>

namespace chronon3d::cli {

struct VideoArgs;

/// Source-compatible name only.  Video planning now produces the canonical
/// RenderJob and no longer owns a parallel orchestration structure.
using VideoJobPlan = chronon3d::RenderJob;

/// Convert the legacy `video` command arguments into RenderMode::Video.
[[nodiscard]] std::optional<RenderJob> make_video_render_job(
    const CompositionRegistry& registry,
    const VideoArgs& args);

/// One-release compatibility name.
[[nodiscard]] std::optional<RenderJob> plan_video_job(
    const CompositionRegistry& registry,
    const VideoArgs& args);

[[nodiscard]] bool validate_video_job(const RenderJob& job);
[[nodiscard]] int dry_run_video_job(const RenderJob& job);
[[nodiscard]] int execute_video_job(const RenderJob& job);

/// Shared render + encode dispatch retained for command_video_camera, which
/// constructs an ad-hoc composition rather than a registry RenderJob.
[[nodiscard]] int render_and_encode_ffmpeg(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget);

} // namespace chronon3d::cli
