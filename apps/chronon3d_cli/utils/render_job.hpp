#pragma once

#include "../commands.hpp"
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
    bool from_specscene{false};
};

std::optional<RenderJobPlan> plan_render_job(const CompositionRegistry& registry,
                                             const RenderArgs& args);

bool execute_render_job(const CompositionRegistry& registry, const RenderJobPlan& plan);

} // namespace chronon3d::cli
