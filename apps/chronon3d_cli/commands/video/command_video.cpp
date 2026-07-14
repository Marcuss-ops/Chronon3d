// ---------------------------------------------------------------------------
// command_video.cpp — Thin dispatcher for video export commands
//
// Item 18 refactoring: the heavy lifting moved to utils/video/video_job_*.cpp
// This file now only wires plan → dry-run / execute.
// ---------------------------------------------------------------------------

#include "../../utils/video/video_job_plan.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>

namespace chronon3d::cli {

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    // Audit §13 Phase 3 verbatim alias-TTL deprecation warning.
    // Single-line form per audit spec example `Deprecated: use chronon render Hero -o Hero.mp4`.
    // Will be REMOVED in V0.2 (TICKET-V3-CLI-UNIFICATION-REMOVE-VIDEO) once
    // `chronon render` natively supports MP4 (RenderMode::Video).
    fmt::print(stderr, "Deprecated: use chronon render {} -o {}.mp4\n", args.comp_id, args.comp_id);
    auto plan = plan_video_job(registry, args);
    (void)args.cpu_budget;
    if (!plan) return 1;

    if (!validate_video_job(*plan)) return 1;

    return plan->dry_run ? dry_run_video_job(*plan) : execute_video_job(*plan);
}

} // namespace chronon3d::cli
