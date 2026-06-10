#pragma once

// ---------------------------------------------------------------------------
// utils/video/video_job_plan.hpp
//
// Item 18 — Video job plan refactoring.
// Extracts plan/validate/execute/dry-run phases from the monolithic
// command_video() so the command function becomes a thin dispatcher.
// ---------------------------------------------------------------------------

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include "../../commands/video/common/video_export_common.hpp"
#include <optional>
#include <string>

namespace chronon3d::cli {

/// Aggregated parameters built during the planning phase.
/// Owns the composition via shared_ptr so downstream phases are safe.
struct VideoJobPlan {
    const CompositionRegistry*            registry{};
    std::shared_ptr<const Composition>    comp;
    std::string                           comp_id;

    RenderSettings                        settings;
    FfmpegExportOptions                   export_options;

    Frame start{0};
    Frame end_exclusive{0};

    bool dry_run{false};
    bool from_specscene{false};
};

/// Phase 1 — Plan.
/// Resolve the composition, build RenderSettings + FfmpegExportOptions,
/// handle end-inclusive-to-exclusive conversion.
/// Returns std::nullopt on resolution failure.
[[nodiscard]] std::optional<VideoJobPlan> plan_video_job(
    const CompositionRegistry& registry,
    const VideoArgs& args);

/// Phase 2 — Validate.
/// Check output path, ffmpeg availability, frame range, ffmpeg-mode,
/// encoder-backend compatibility, and exporter existence.
/// Returns true if the plan is ready to execute.
[[nodiscard]] bool validate_video_job(const VideoJobPlan& plan);

/// Phase 3 — Dry-run.
/// Log all plan details and attempt a single-frame render to catch errors
/// early without performing the full export.
/// Returns 0 on success, non-zero on error.
[[nodiscard]] int dry_run_video_job(const VideoJobPlan& plan);

/// Phase 4 — Execute.
/// Validate, install signal handlers, dispatch to the exporter pipeline.
/// Returns 0 on success, non-zero on error.
[[nodiscard]] int execute_video_job(const VideoJobPlan& plan);

// ── Legacy entry point (kept for command_video_camera) ──────────────────────

/// Exporter lookup helper — returns a single static registry shared
/// across validate/execute paths (avoids duplicate singletons).
class ExporterRegistry;
[[nodiscard]] ExporterRegistry& shared_exporter_registry();

/// Shared render + encode dispatch used by both the job executor and
/// the video-camera command (which builds its own composition on the fly).
/// Does NOT validate — callers must call validate_video_job() first.
[[nodiscard]] int render_and_encode_ffmpeg(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts);

} // namespace chronon3d::cli
