#pragma once

#include <boost/property_tree/ptree.hpp>
#include <chronon3d/assets/svg_path_loader.hpp>
#include <cstddef>
#include <optional>
#include <string>

namespace chronon3d::assets::svg_document_detail {
[[nodiscard]] std::optional<std::string> validate_svg_subset(
    const boost::property_tree::ptree& tree);
[[nodiscard]] std::optional<std::string> parse_root_svg_filter(
    const boost::property_tree::ptree& filter,
    std::vector<SvgFilterEffectSpec>& effects);
[[nodiscard]] std::optional<std::string> inspect_svg_xml_tree(
    const boost::property_tree::ptree& document, std::size_t max_nodes);
} // namespace chronon3d::assets::svg_document_detail
