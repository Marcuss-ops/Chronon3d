#pragma once

#include "../../commands.hpp"
#include "cli_render_utils.hpp"

#include <chronon3d/backends/software/render_settings.hpp>

#include <memory>
#include <optional>
#include <string>

namespace chronon3d::cli {

struct RenderJobPlan {
    FrameRange range;
    std::shared_ptr<Composition> comp;
    std::string comp_id;
    std::string output;
    RenderSettings settings;
    std::string log_level;
    bool benchmark_all{false};
    bool report{false};
    bool diagnostic_plan{false};

    std::string command_line; // reconstructed from argv (for telemetry)

    // Renderer warmup
    bool   warmup_renderer{false};
    size_t warmup_framebuffers{8};
    bool   warmup_dummy_frame{false};
};

std::optional<RenderJobPlan> plan_render_job(const CompositionRegistry& registry,
                                             const RenderArgs& args);

bool execute_render_job(const CompositionRegistry& registry, const RenderJobPlan& plan);

} // namespace chronon3d::cli
