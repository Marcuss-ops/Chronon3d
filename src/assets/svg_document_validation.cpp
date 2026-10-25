#include "svg_document_validation.hpp"
#include "svg_path_loader_detail.hpp"
#include "svg_text_detail.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <cmath>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::assets::svg_document_detail {
using svg_loader_detail::parse_svg_color;
using svg_loader_detail::parse_svg_length;
using svg_loader_detail::style_properties;
using svg_loader_detail::trim;
using svg_loader_detail::transform_numbers;
using svg_text_detail::svg_baseline_shift;
using svg_text_detail::svg_text_path_offset;
using svg_text_detail::svg_text_path_reference;

std::optional<std::string> validate_svg_subset(
    const boost::property_tree::ptree& tree) {
    static const std::set<std::string> supported_elements{
        "svg", "g", "path", "rect", "circle", "ellipse", "line",
        "polyline", "polygon", "animate", "animateTransform", "defs", "style",
        "linearGradient", "radialGradient", "stop", "pattern", "use", "clipPath", "mask",
        "filter", "feGaussianBlur", "feDropShadow", "feColorMatrix",
        "feBlend",
        "text", "tspan", "textPath", "image"};
    static const std::set<std::string> supported_properties{
        "fill", "stroke", "stroke-width", "stroke-opacity", "fill-opacity",
        "opacity", "stop-color", "stop-opacity", "clip-path", "mask", "filter",
        "stroke-linecap", "stroke-linejoin", "stroke-dasharray", "stroke-dashoffset",
        "stroke-miterlimit", "fill-rule", "color", "display", "visibility", "paint-order",
        "clip-rule",
        "font-family", "font-size",
        "font-weight", "font-style", "direction", "text-anchor", "dominant-baseline", "baseline-shift"};
    static const std::set<std::string> unsupported_paint_attributes{
        "mix-blend-mode",
        "vector-effect"};
    const auto valid_unit_interval = [](const std::string& text) {
        try {
            std::size_t consumed = 0;
            const float value = std::stof(text, &consumed);
            return consumed == text.size() && std::isfinite(value) &&
                   value >= 0.0f && value <= 1.0f;
        } catch (const std::invalid_argument&) {
            return false;
        } catch (const std::out_of_range&) {
            return false;
        }
    };
    const auto valid_stroke_width = [](std::string text) {
        const auto val = parse_svg_length(text);
        return val.has_value() && *val >= 0.0f && *val <= 100000.0f;
    };
    std::function<std::optional<std::string>(const boost::property_tree::ptree&, const std::string&)> visit;
    visit = [&](const boost::property_tree::ptree& current,
                const std::string& parent_name) -> std::optional<std::string> {
        for (const auto& [name, node] : current) {
            if (name == "<xmlattr>") continue;
            if (name == "<xmlcomment>") continue;
            if (name == "<xmltext>") {
                if (parent_name == "style" || parent_name == "text" ||
                    parent_name == "tspan" || parent_name == "textPath" ||
                    trim(node.data()).empty()) continue;
                return "SVG text content is supported only inside <text> or <tspan>";
            }
            if (!supported_elements.contains(name))
                return "SVG element is not supported: <" + name + ">";
            if (name == "style") {
                if (parent_name != "svg") return "SVG stylesheet elements must be direct children of <svg>";
                continue;
            }
            if (name == "animate" || name == "animateTransform") {
                if (parent_name != "path" && parent_name != "g" && parent_name != "stop")
                    return "SVG animation is supported only as a direct child of <path>, <g>, or gradient <stop>";
                if (parent_name == "g" &&
                    (name == "animateTransform"
                        ? node.get<std::string>("<xmlattr>.attributeName", "") != "transform"
                        : (node.get<std::string>("<xmlattr>.attributeName", "") != "opacity")))
                    return "SVG groups support only transform and opacity animation";
                if (parent_name == "stop" &&
                    (name != "animate" ||
                     (node.get<std::string>("<xmlattr>.attributeName", "") != "stop-color" &&
                      node.get<std::string>("<xmlattr>.attributeName", "") != "stop-opacity")))
                    return "SVG gradient stops support only stop-color and stop-opacity animation";
                continue;
            }
            if (name == "tspan" && parent_name != "text" && parent_name != "tspan")
                return "SVG <tspan> must be nested inside <text> or <tspan>";
            if (name == "textPath" && parent_name != "text" && parent_name != "tspan")
                return "SVG <textPath> must be nested inside <text> or <tspan>";
            if (name == "textPath") {
                if (!svg_text_path_reference(node))
                    return "SVG <textPath> requires a local href='#id' path reference";
                if (!svg_text_path_offset(node))
                    return "SVG <textPath> startOffset must be a finite bounded number or percentage";
                const auto method = node.get<std::string>("<xmlattr>.method", "align");
                const auto spacing = node.get<std::string>("<xmlattr>.spacing", "exact");
                const auto side = node.get<std::string>("<xmlattr>.side", "left");
                if (method != "align" || spacing != "exact" || side != "left")
                    return "SVG <textPath> supports only method=align, spacing=exact, and side=left";
            }
            if (name == "text" && parent_name != "svg" && parent_name != "g")
                return "SVG <text> must be a child of <svg> or <g>";
            if (name == "text" || name == "tspan" || name == "textPath") {
                const auto properties = style_properties(node);
                if (const auto it = properties.find("font-size"); it != properties.end()) {
                    std::string value = trim(it->second);
                    if (value.ends_with("px")) value.resize(value.size() - 2);
                    try {
                        std::size_t used = 0;
                        const float size = std::stof(value, &used);
                        if (used != value.size() || !std::isfinite(size) || size <= 0.0f || size > 4096.0f)
                            return "SVG text font-size must be a positive bounded pixel value";
                    } catch (const std::invalid_argument&) { return "SVG text font-size must be a positive bounded pixel value"; } catch (const std::out_of_range&) { return "SVG text font-size must be a positive bounded pixel value"; }
                }
                if (const auto it = properties.find("text-anchor"); it != properties.end() &&
                    it->second != "start" && it->second != "middle" && it->second != "end")
                    return "SVG text-anchor supports start, middle, or end";
                if (const auto it = properties.find("direction"); it != properties.end() &&
                    it->second != "ltr" && it->second != "rtl")
                    return "SVG text direction supports ltr or rtl";
                if (const auto it = properties.find("dominant-baseline"); it != properties.end() &&
                    it->second != "auto" && it->second != "use-script" &&
                    it->second != "no-change" && it->second != "reset-size" &&
                    it->second != "ideographic" && it->second != "alphabetic" &&
                    it->second != "central" && it->second != "middle" &&
                    it->second != "hanging" && it->second != "mathematical" &&
                    it->second != "text-before-edge" && it->second != "text-after-edge")
                    return "SVG dominant-baseline has an unsupported baseline identifier";
                if (const auto it = properties.find("font-style"); it != properties.end() &&
                    it->second != "normal" && it->second != "italic" && it->second != "oblique")
                    return "SVG font-style supports normal, italic, or oblique";
                if (const auto it = properties.find("font-weight"); it != properties.end()) {
                    const auto weight = trim(it->second);
                    if (weight != "normal" && weight != "bold") {
                        try {
                            std::size_t used = 0;
                            const int parsed = std::stoi(weight, &used);
                            if (used != weight.size() || parsed < 100 || parsed > 900 || parsed % 100 != 0)
                                return "SVG font-weight must be normal, bold, or 100..900 by hundreds";
                        } catch (const std::invalid_argument&) { return "SVG font-weight must be normal, bold, or 100..900 by hundreds"; } catch (const std::out_of_range&) { return "SVG font-weight must be normal, bold, or 100..900 by hundreds"; }
                    }
                }
                if (const auto it = properties.find("baseline-shift"); it != properties.end() &&
                    !svg_baseline_shift(it->second, 16.0f))
                    return "SVG baseline-shift supports baseline, sub, super, px, em, or percent values";
            }
            if (name == "use") {
                const auto href = node.get_optional<std::string>("<xmlattr>.href");
                if (!href || href->size() < 2 || href->front() != '#' ||
                    href->find_first_of(" \t\r\n", 1) != std::string::npos)
                    return "SVG <use> supports only local #id references";
                for (const auto* coordinate : {"x", "y"}) {
                    if (const auto value = node.get_optional<std::string>(std::string("<xmlattr>.") + coordinate)) {
                        try {
                            std::size_t used = 0;
                            const float parsed = std::stof(*value, &used);
                            if (used != value->size() || !std::isfinite(parsed) || std::abs(parsed) > 100000.0f)
                                return "SVG <use> coordinates must be finite and bounded";
                        } catch (const std::invalid_argument&) { return "SVG <use> coordinates must be finite and bounded"; } catch (const std::out_of_range&) { return "SVG <use> coordinates must be finite and bounded"; }
                    }
                }
            }

            if (const auto style = node.template get_optional<std::string>("<xmlattr>.style")) {
                std::istringstream declarations(*style);
                std::string declaration;
                while (std::getline(declarations, declaration, ';')) {
                    if (trim(declaration).empty()) continue;
                    const auto colon = declaration.find(':');
                    if (colon == std::string::npos)
                        return "SVG style declaration is malformed";
                    auto key = trim(declaration.substr(0, colon));
                    std::transform(key.begin(), key.end(), key.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (!supported_properties.contains(key))
                        return "SVG style property is not supported: " + key;
                }
            }
            if (const auto attributes = node.get_child_optional("<xmlattr>")) {
                for (const auto& [key, value] : *attributes) {
                    (void)value;
                    if (unsupported_paint_attributes.contains(key))
                        return "SVG presentation attribute is not supported: " + key;
                }
            }
            const auto properties = style_properties(node);
            if (const auto it = properties.find("display"); it != properties.end()) {
                auto value = trim(it->second);
                std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (value != "none" && value != "inline" && value != "block")
                    return "SVG display supports none, inline, or block";
            }
            if (const auto it = properties.find("visibility"); it != properties.end()) {
                auto value = trim(it->second);
                std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (value != "visible" && value != "hidden" && value != "collapse")
                    return "SVG visibility supports visible, hidden, or collapse";
            }
            for (const auto* key : {"clip-path", "mask"}) {
                if (const auto it = properties.find(key); it != properties.end()) {
                    const auto value = trim(it->second);
                    const bool valid_target = (name == "svg" || name == "g" || name == "path" ||
                                               name == "rect" || name == "circle" || name == "ellipse" ||
                                               name == "polygon" || name == "polyline" || name == "line");
                    if (!valid_target ||
                        !value.starts_with("url(#") || !value.ends_with(')') || value.size() <= 6)
                        return std::string("SVG ") + key + " supports only root, group, or path references to url(#id)";
                }
            }
            if (const auto it = properties.find("filter"); it != properties.end()) {
                const auto val = trim(it->second);
                const bool valid_target = (name == "svg" || name == "g" || name == "path" ||
                                           name == "rect" || name == "circle" || name == "ellipse" ||
                                           name == "polygon" || name == "polyline" || name == "line" ||
                                           name == "image");
                if (!valid_target || (val != "none" &&
                    (!val.starts_with("url(#") || !val.ends_with(')') || val.size() <= 6)))
                    return "SVG filters currently support only a root or element url(#id) reference";
            }
            if (name == "clipPath" || name == "mask") {
                if (parent_name != "defs") return "SVG clipPath and mask definitions must be children of <defs>";
            }
            if (name == "pattern" && parent_name != "defs")
                return "SVG pattern definitions must be children of <defs>";
            if (parent_name == "pattern" && name != "rect" && name != "path" &&
                name != "circle" && name != "ellipse" && name != "line" &&
                name != "polyline" && name != "polygon")
                return "SVG text patterns support only path and primitive tile content";
            if (name == "filter" && parent_name != "defs")
                return "SVG filter definitions must be children of <defs>";
            if (name == "feGaussianBlur" && parent_name != "filter")
                return "SVG feGaussianBlur must be a direct child of <filter>";
            if (name == "stop") {
                if (const auto it = properties.find("stop-color"); it != properties.end() &&
                    !parse_svg_color(it->second, Color::black()))
                    return "SVG gradient stop color is invalid or unsupported";
                if (const auto it = properties.find("stop-opacity"); it != properties.end() &&
                    !valid_unit_interval(trim(it->second)))
                    return "SVG stop-opacity must be a finite number in [0,1]";
            } else if (properties.contains("stop-color") || properties.contains("stop-opacity")) {
                return "SVG stop-color and stop-opacity are supported only on <stop>";
            }
            for (const auto* key : {"fill", "stroke"}) {
                const auto it = properties.find(key);
                if (it == properties.end()) continue;
                auto value = trim(it->second);
                std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const bool local_paint_reference = value.starts_with("url(#") && value.ends_with(")") &&
                    (key == "fill" || (key == "stroke" &&
                        (name == "text" || name == "tspan" || name == "textPath")));
                if (value == "none" || local_paint_reference) continue;
                if (name == "stop" && key == "fill") continue;
                if (!parse_svg_color(it->second, Color::black()))
                    return std::string("SVG ") + key + " color is invalid or unsupported";
            }
            if (const auto it = properties.find("color"); it != properties.end() &&
                !parse_svg_color(it->second, Color::black()))
                return "SVG color is invalid or unsupported";
            for (const auto* key : {"opacity", "fill-opacity", "stroke-opacity"}) {
                const auto it = properties.find(key);
                if (it != properties.end() && !valid_unit_interval(trim(it->second)))
                    return std::string("SVG ") + key + " must be a finite number in [0,1]";
            }
            if (const auto it = properties.find("stroke-width");
                it != properties.end() && !valid_stroke_width(trim(it->second)))
                return "SVG stroke-width must be finite and within [0,100000]";
            if (const auto it = properties.find("stroke-linecap"); it != properties.end() &&
                it->second != "butt" && it->second != "round" && it->second != "square")
                return "SVG stroke-linecap must be butt, round, or square";
            if (const auto it = properties.find("stroke-linejoin"); it != properties.end() &&
                it->second != "miter" && it->second != "round" && it->second != "bevel")
                return "SVG stroke-linejoin must be miter, round, or bevel";
            if (const auto it = properties.find("stroke-miterlimit"); it != properties.end()) {
                try {
                    std::size_t consumed = 0;
                    const float limit = std::stof(trim(it->second), &consumed);
                    if (consumed != trim(it->second).size() || !std::isfinite(limit) ||
                        limit < 1.0f || limit > 1000.0f)
                        return "SVG stroke-miterlimit must be within [1,1000]";
                } catch (const std::invalid_argument&) {
                    return "SVG stroke-miterlimit must be within [1,1000]";
                } catch (const std::out_of_range&) {
                    return "SVG stroke-miterlimit must be within [1,1000]";
                }
            }
            if (const auto it = properties.find("fill-rule"); it != properties.end() &&
                it->second != "evenodd" && it->second != "nonzero")
                return "SVG fill-rule must be evenodd or nonzero";
            if (const auto it = properties.find("clip-rule"); it != properties.end() &&
                it->second != "evenodd" && it->second != "nonzero")
                return "SVG clip-rule must be evenodd or nonzero";
            if (const auto it = properties.find("stroke-dashoffset"); it != properties.end()) {
                try {
                    std::size_t consumed = 0;
                    const float offset = std::stof(trim(it->second), &consumed);
                    if (consumed != trim(it->second).size() || !std::isfinite(offset) ||
                        std::abs(offset) > 100000.0f)
                        return "SVG stroke-dashoffset must be a finite bounded number";
                } catch (const std::invalid_argument&) {
                    return "SVG stroke-dashoffset must be a finite bounded number";
                } catch (const std::out_of_range&) {
                    return "SVG stroke-dashoffset must be a finite bounded number";
                }
            }
            if (const auto it = properties.find("stroke-dasharray"); it != properties.end() &&
                trim(it->second) != "none") {
                const auto dashes = transform_numbers(it->second);
                if (dashes.empty() || dashes.size() > 32 ||
                    std::any_of(dashes.begin(), dashes.end(), [](float dash) {
                        return !std::isfinite(dash) || dash < 0.0f || dash > 100000.0f;
                    }) || std::all_of(dashes.begin(), dashes.end(), [](float dash) { return dash == 0.0f; }))
                    return "SVG stroke-dasharray must contain 1..32 finite non-negative values, not all zero";
            }

            if (name == "linearGradient" || name == "radialGradient" || name == "stop") continue;
            if (auto error = visit(node, name)) return error;
        }
        return std::nullopt;
    };
    return visit(tree, {});
}


std::optional<std::string> inspect_svg_xml_tree(
    const boost::property_tree::ptree& document, std::size_t max_nodes) {
    std::string unsupported;
    std::size_t node_count = 0;
    std::function<void(const boost::property_tree::ptree&, const std::string&)> inspect =
        [&](const auto& tree, const std::string& parent_name) {
            for (const auto& [name, node] : tree) {
                if (name == "<xmlattr>" || name == "<xmlcomment>") continue;
                if (name == "<xmltext>") {
                    if (!trim(node.data()).empty() && parent_name != "text" &&
                        parent_name != "tspan" && parent_name != "textPath")
                        unsupported = "text content outside <text>/<tspan>";
                    if (!unsupported.empty()) return;
                    continue;
                }
                if (++node_count > max_nodes) {
                    unsupported = "resource limit: XML node count exceeds 100000";
                    return;
                }
                if (name == "image") {
                    const auto href = node.template get_optional<std::string>("<xmlattr>.href");
                    const auto xlink_href = node.template get_optional<std::string>(
                        "<xmlattr>.{http://www.w3.org/1999/xlink}href");
                    const std::string image_href = href ? *href : (xlink_href ? *xlink_href : "");
                    if (image_href.empty() || !image_href.starts_with("data:image/")) {
                        unsupported = "non supportata: <image>";
                        return;
                    }
                    continue;
                }
                if (name == "style") continue;
                const auto href = node.template get_optional<std::string>("<xmlattr>.href");
                const auto xlink_href = node.template get_optional<std::string>(
                    "<xmlattr>.{http://www.w3.org/1999/xlink}href");
                if (href || xlink_href) {
                    const auto reference = href ? href : xlink_href;
                    const bool local_reference =
                        (name == "use" || name == "textPath") && reference->size() >= 2 &&
                        reference->front() == '#' &&
                        reference->find_first_of(" \t\r\n", 1) == std::string::npos;
                    const bool embedded_image =
                        name == "image" && reference->starts_with("data:image/");
                    if (!local_reference && !embedded_image) {
                        unsupported = "CSS esterni";
                        return;
                    }
                }
                inspect(node, name);
                if (!unsupported.empty()) return;
            }
        };
    inspect(document, {});
    if (unsupported.empty()) return std::nullopt;
    return unsupported;
}

} // namespace chronon3d::assets::svg_document_detail
