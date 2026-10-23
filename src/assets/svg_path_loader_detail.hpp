#pragma once

#include <boost/property_tree/ptree.hpp>
#include <chronon3d/math/color.hpp>

#include <map>
#include <optional>
#include <string_view>
#include <vector>
#include <string>

namespace chronon3d::assets::svg_loader_detail {

inline std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] std::optional<std::string> apply_svg_css_rules(
    boost::property_tree::ptree& document);
[[nodiscard]] std::vector<float> transform_numbers(std::string_view text);
[[nodiscard]] std::optional<float> parse_svg_length(
    std::string_view text, float percentage_relative_to = -1.0f);
[[nodiscard]] std::optional<chronon3d::Color> parse_svg_color(
    std::string value, chronon3d::Color current);
[[nodiscard]] std::map<std::string, std::string> style_properties(
    const boost::property_tree::ptree& node);
[[nodiscard]] float style_number(
    const std::map<std::string, std::string>& style, const char* key, float fallback);

}  // namespace chronon3d::assets::svg_loader_detail
