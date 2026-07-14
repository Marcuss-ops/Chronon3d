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
    // Audit §13 Phase 3: alias-TTL deprecation warning.
    // Will be REMOVED in V0.2 (TICKET-V3-CLI-UNIFICATION-REMOVE-VIDEO) once
    // `chronon render` natively supports MP4 (RenderMode::Video).
    // Canonical equivalent: `chronon render <id> -o <id>.mp4`
    fmt::print(stderr,
        "WARNING: 'chronon video' is DEPRECATED and will be removed in the next release.\n"
        "         Use 'chronon render {} -o {}.mp4' instead.\n"
        "         See TICKET-V3-CLI-UNIFICATION-ALIASES-PHASE-3.\n\n",
        args.comp_id, args.comp_id);
    auto plan = plan_video_job(registry, args);
    (void)args.cpu_budget;
    if (!plan) return 1;

    if (!validate_video_job(*plan)) return 1;

    return plan->dry_run ? dry_run_video_job(*plan) : execute_video_job(*plan);
}

} // namespace chronon3d::cli
