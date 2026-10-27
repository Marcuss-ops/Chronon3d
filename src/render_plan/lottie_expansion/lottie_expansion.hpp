#pragma once

#include <chronon3d/render_plan/render_plan.hpp>
#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::render_plan::detail {

// Private boundary for lowering a source Lottie asset into RenderPlan layers.
[[nodiscard]] std::optional<std::string> expand_static_lottie(
    const LayerPlan& layer, const std::filesystem::path& path,
    std::vector<LayerPlan>& expanded_layers);

} // namespace chronon3d::render_plan::detail
