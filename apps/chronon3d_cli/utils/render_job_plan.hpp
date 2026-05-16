#pragma once

#include <chronon3d/backends/software/render_settings.hpp>
#include <apps/chronon3d_cli/utils/cli_utils.hpp>
#include <apps/chronon3d_cli/commands.hpp>
#include <string>

namespace chronon3d::cli {

struct RenderJobPlan {
    std::string comp_id;
    FrameRange range;
    std::string output;
    RenderSettings settings;
};

struct RenderJobPlanResult {
    bool ok{false};
    RenderJobPlan value{};
    std::string error;
};

/// Validates RenderArgs and produces a clean RenderJobPlan.
/// motion_blur_allowed: set to false when the composition type doesn't support it (e.g. specscene).
RenderJobPlanResult plan_render_job(const RenderArgs& args, bool motion_blur_allowed);

} // namespace chronon3d::cli
