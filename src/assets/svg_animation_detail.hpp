#pragma once

#include <chronon3d/assets/svg_path_loader.hpp>
#include <boost/property_tree/ptree.hpp>

namespace chronon3d::assets::svg_animation_detail {

[[nodiscard]] bool parse_svg_animation(
    const std::string& tag, const boost::property_tree::ptree& node,
    SvgAnimationSpec& output);
[[nodiscard]] bool parse_svg_morph_animation(
    const boost::property_tree::ptree& node, SvgMorphAnimationSpec& output);

}  // namespace chronon3d::assets::svg_animation_detail
