#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>

namespace chronon3d::render_plan::detail {

[[nodiscard]] bool invalid_logical_path(const std::string& value);
[[nodiscard]] LayerType layer_type(const std::string& value);
[[nodiscard]] FitMode fit_mode(const std::string& value);
[[nodiscard]] std::optional<chronon3d::BlendMode> blend_mode(const std::string& value);
[[nodiscard]] OutputFormat output_format(const std::string& value);
[[nodiscard]] VideoCodec video_codec(const std::string& value);
[[nodiscard]] LayerPlan decode_layer(const nlohmann::json& value);

}  // namespace chronon3d::render_plan::detail
