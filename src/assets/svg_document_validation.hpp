#pragma once

#include <boost/property_tree/ptree.hpp>
#include <cstddef>
#include <optional>
#include <string>

namespace chronon3d::assets::svg_document_detail {
[[nodiscard]] std::optional<std::string> validate_svg_subset(
    const boost::property_tree::ptree& tree);
[[nodiscard]] std::optional<std::string> parse_root_gaussian_filter(
    const boost::property_tree::ptree& filter, float& std_deviation);
[[nodiscard]] std::optional<std::string> inspect_svg_xml_tree(
    const boost::property_tree::ptree& document, std::size_t max_nodes);
} // namespace chronon3d::assets::svg_document_detail
