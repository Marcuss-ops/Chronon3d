// ---------------------------------------------------------------------------
// command_video.cpp — Thin dispatcher for video export commands
//
// Item 18 refactoring: the heavy lifting moved to utils/video/video_job_*.cpp
// This file now only wires plan → dry-run / execute.
// ---------------------------------------------------------------------------

#include "../../utils/video/video_job_plan.hpp"
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    auto plan = plan_video_job(registry, args);
    (void)args.cpu_budget;
    if (!plan) return 1;

    if (!validate_video_job(*plan)) return 1;

    return plan->dry_run ? dry_run_video_job(*plan) : execute_video_job(*plan);
}

} // namespace chronon3d::cli
