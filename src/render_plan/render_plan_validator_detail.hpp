#pragma once

#include <nlohmann/json_fwd.hpp>

namespace chronon3d::render_plan::validation_detail {
nlohmann::json v2_projection(const nlohmann::json& root);
}
