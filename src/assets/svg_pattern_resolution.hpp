#pragma once

#include "svg_pattern_detail.hpp"

namespace chronon3d::assets::svg_gradient_detail {

struct ResolvedPattern {
    const boost::property_tree::ptree* content{nullptr};
    std::map<std::string, std::string> attributes;
    Color inherited_color{Color::black()};
};

[[nodiscard]] bool resolve_pattern_href(
    const std::string& id, const SvgPatternMap& patterns,
    ResolvedPattern& resolved, std::string& error);
[[nodiscard]] std::string pattern_attribute(
    const ResolvedPattern& pattern, const char* key, const char* fallback);
[[nodiscard]] bool reject_percent_user_unit(
    const std::string& value, const char* key, std::string& error);
[[nodiscard]] bool parse_user_dimension(
    const std::string& text, std::uint32_t& output, const char* key,
    std::string& error);
[[nodiscard]] bool parse_object_dimension(
    const std::string& text, float& fraction, std::uint32_t& pixels,
    const char* key, std::string& error);

}  // namespace chronon3d::assets::svg_gradient_detail
