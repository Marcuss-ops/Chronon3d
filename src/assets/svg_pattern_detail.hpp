#pragma once

#include "svg_gradient_detail.hpp"

#include <optional>
#include <string>

namespace chronon3d::assets::svg_gradient_detail {

/// Pattern definitions are collected in a first pass so that gradients in
/// `<defs>` are fully known before a tile is rasterized, regardless of the
/// document order of `<pattern>` and `<linearGradient>`/`<radialGradient>`.
struct SvgPatternDefinition {
    const boost::property_tree::ptree* node{nullptr};
    Color inherited_color{Color::black()};
};

using SvgPatternMap = std::map<std::string, SvgPatternDefinition>;

/// Resolve pattern href chains, rasterize every bounded tile and insert the
/// resulting specs into `gradients` (patterns and gradients share the paint
/// reference map). Returns an error string for unsupported or malformed input.
[[nodiscard]] std::optional<std::string> import_svg_patterns(
    const SvgPatternMap& patterns, SvgGradientMap& gradients);

}  // namespace chronon3d::assets::svg_gradient_detail
