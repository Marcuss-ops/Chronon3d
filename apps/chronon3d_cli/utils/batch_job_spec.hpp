#pragma once

#include "../commands.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::cli {

std::optional<RenderArgs> parse_batch_job_spec(std::string_view spec, std::string* error = nullptr);

} // namespace chronon3d::cli
