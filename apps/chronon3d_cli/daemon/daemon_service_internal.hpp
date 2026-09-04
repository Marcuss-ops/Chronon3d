#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::cli::daemon_detail {

std::vector<std::string> split_args(const std::string& line);
std::string format_output_path(const std::string& pattern, std::int32_t frame);

} // namespace chronon3d::cli::daemon_detail
