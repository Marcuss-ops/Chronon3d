#include "svg_pattern_resolution.hpp"
#include "svg_importer.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace chronon3d::assets::svg_gradient_detail {

using svg_loader_detail::style_properties;
using svg_loader_detail::trim;

namespace {
constexpr float kObjectBBoxRaster = 256.0f;
constexpr std::size_t kMaxPatternHrefDepth = 8;
}

[[nodiscard]] bool has_element_children(const boost::property_tree::ptree& node) {
    for (const auto& [name, child] : node) {
        (void)child;
        if (name == "<xmlattr>" || name == "<xmltext>" || name == "<xmlcomment>")
            continue;
        return true;
    }
    return false;
}

[[nodiscard]] bool read_local_href(const boost::property_tree::ptree& node,
                                   std::optional<std::string>& id,
                                   std::string& error) {
    auto href = node.get_optional<std::string>("<xmlattr>.href");
    if (!href) href = node.get_optional<std::string>("<xmlattr>.xlink:href");
    if (!href) href = node.get_optional<std::string>(
        "<xmlattr>.{http://www.w3.org/1999/xlink}href");
    if (!href) return true;
    const auto value = trim(*href);
    if (value.size() < 2 || value.front() != '#' ||
        value.find_first_of(" \t\r\n#", 1) != std::string::npos) {
        error = "SVG pattern href must be a local #id reference";
        return false;
    }
    id = value.substr(1);
    return true;
}

[[nodiscard]] bool resolve_pattern_href(
    const std::string& id, const SvgPatternMap& patterns,
    ResolvedPattern& resolved, std::string& error) {
    std::set<std::string> visited;
    std::string current = id;
    for (std::size_t depth = 0; depth < kMaxPatternHrefDepth; ++depth) {
        if (!visited.insert(current).second) {
            error = "SVG pattern href contains a cycle at #" + current;
            return false;
        }
        const auto found = patterns.find(current);
        if (found == patterns.end()) {
            error = "SVG pattern href references an unknown pattern: #" + current;
            return false;
        }
        const auto& node = *found->second.node;
        if (depth == 0) resolved.inherited_color = found->second.inherited_color;
        if (node.get_optional<std::string>("<xmlattr>.viewBox") ||
            node.get_optional<std::string>("<xmlattr>.preserveAspectRatio")) {
            error = "SVG text patterns do not support viewBox or preserveAspectRatio";
            return false;
        }
        if (!style_properties(node).empty()) {
            error = "SVG text pattern-level paint/style inheritance is not supported";
            return false;
        }
        for (const auto* key : {"width", "height", "x", "y", "patternUnits",
                                "patternContentUnits", "patternTransform"}) {
            if (resolved.attributes.contains(key)) continue;
            if (const auto value = node.get_optional<std::string>(
                    std::string("<xmlattr>.") + key))
                resolved.attributes.emplace(key, *value);
        }
        if (!resolved.content && has_element_children(node))
            resolved.content = &node;
        std::optional<std::string> href;
        if (!read_local_href(node, href, error)) return false;
        if (!href) break;
        current = *href;
    }
    if (resolved.content == nullptr) {
        error = "SVG pattern href chain has no tile content";
        return false;
    }
    return true;
}

[[nodiscard]] std::string pattern_attribute(
    const ResolvedPattern& pattern, const char* key, const char* fallback) {
    if (const auto found = pattern.attributes.find(key);
        found != pattern.attributes.end())
        return found->second;
    return fallback;
}

[[nodiscard]] bool reject_percent_user_unit(
    const std::string& value, const char* key, std::string& error) {
    if (trim(value).find('%') != std::string::npos) {
        error = std::string("SVG text pattern ") + key +
            " does not support percentages with userSpaceOnUse units";
        return false;
    }
    return true;
}

[[nodiscard]] bool parse_user_dimension(
    const std::string& text, std::uint32_t& output, const char* key,
    std::string& error) {
    if (!reject_percent_user_unit(text, key, error)) return false;
    float value = 0.0f;
    if (!parse_svg_scalar(text, 0.0f, value) || value < 1.0f ||
        value > 256.0f || std::floor(value) != value) {
        error = std::string("SVG text pattern ") + key +
            " must be an integer user-space pixel value in [1,256]";
        return false;
    }
    output = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] bool parse_object_dimension(
    const std::string& text, float& fraction, std::uint32_t& pixels,
    const char* key, std::string& error) {
    float value = 0.0f;
    if (!parse_svg_scalar(text, 0.0f, value) || !(value > 0.0f) ||
        value > 1.0f) {
        error = std::string("SVG text pattern objectBoundingBox ") + key +
            " must be a positive fraction in (0,1]";
        return false;
    }
    fraction = value;
    const float scaled = value * kObjectBBoxRaster;
    if (scaled < 1.0f) {
        error = std::string("SVG text pattern objectBoundingBox ") + key +
            " is below the bounded raster resolution";
        return false;
    }
    pixels = static_cast<std::uint32_t>(
        std::clamp(std::lround(scaled), 1L, 256L));
    return true;
}


}  // namespace chronon3d::assets::svg_gradient_detail
