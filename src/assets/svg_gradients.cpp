#include "svg_gradient_detail.hpp"
#include "svg_pattern_detail.hpp"
#include "svg_importer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <set>
#include <string>
#include <utility>

namespace chronon3d::assets::svg_gradient_detail {
using svg_loader_detail::parse_svg_color;
using svg_loader_detail::parse_svg_length;
using svg_loader_detail::style_properties;
using svg_loader_detail::trim;
using svg_loader_detail::transform_numbers;

bool parse_svg_gradient_reference(const std::string& text, std::string& id) {
    const auto value = trim(text);
    if (!value.starts_with("url(#") || !value.ends_with(')') || value.size() <= 6)
        return false;
    id = value.substr(5, value.size() - 6);
    return !id.empty() && id.find_first_of(" \t\r\n") == std::string::npos;
}

bool parse_svg_scalar(std::string text, float fallback, float& value) {
    text = trim(std::move(text));
    bool percent = !text.empty() && text.back() == '%';
    if (percent) text.pop_back();
    try {
        std::size_t used = 0;
        value = std::stof(text, &used);
        if (used != text.size() || !std::isfinite(value)) return false;
        if (percent) value /= 100.0f;
        (void)fallback;
        return std::abs(value) <= 100000.0f;
    } catch (const std::invalid_argument&) { return false; } catch (const std::out_of_range&) { return false; }
}

std::optional<std::string> collect_svg_gradients_impl(
    const boost::property_tree::ptree& tree, SvgGradientMap& gradients,
    SvgPatternMap& patterns, Color inherited_color) {
    for (const auto& [name, node] : tree) {
        Color current_color = inherited_color;
        const auto node_properties = style_properties(node);
        if (const auto color_it = node_properties.find("color");
            color_it != node_properties.end()) {
            if (const auto color = parse_svg_color(color_it->second, current_color))
                current_color = *color;
        }
        if (name == "pattern") {
            const auto id = node.get_optional<std::string>("<xmlattr>.id");
            if (!id || id->empty() || id->size() > 128 || patterns.contains(*id))
                return "SVG pattern requires a unique bounded id";
            patterns.emplace(*id, SvgPatternDefinition{&node, current_color});
            continue;
        }
        if (name == "linearGradient" || name == "radialGradient") {
            const auto id = node.get_optional<std::string>("<xmlattr>.id");
            if (!id || id->empty() || id->size() > 128 || gradients.contains(*id))
                return "SVG gradient requires a unique bounded id";
            const auto units = node.get<std::string>("<xmlattr>.gradientUnits", "objectBoundingBox");
            const auto spread_method = node.get<std::string>("<xmlattr>.spreadMethod", "pad");
            if ((units != "objectBoundingBox" && units != "userSpaceOnUse") ||
                (spread_method != "pad" && spread_method != "repeat" && spread_method != "reflect") ||
                node.get_optional<std::string>("<xmlattr>.href") ||
                node.get_optional<std::string>("<xmlattr>.{http://www.w3.org/1999/xlink}href"))
                return "SVG gradient supports objectBoundingBox or userSpaceOnUse units, pad/repeat/reflect spread, and no inheritance";
            SvgGradientSpec gradient;
            gradient.radial = name == "radialGradient";
            gradient.user_space = (units == "userSpaceOnUse");
            gradient.spread = spread_method == "repeat" ? graphics::GradientSpread::Repeat
                : spread_method == "reflect" ? graphics::GradientSpread::Reflect
                : graphics::GradientSpread::Pad;
            if (const auto transform = node.get_optional<std::string>("<xmlattr>.gradientTransform"))
                gradient.transform = parse_transform(*transform);
            auto coordinate = [&](const char* key, const char* fallback, float& out) {
                const auto val_str = node.get<std::string>(std::string("<xmlattr>.") + key, fallback);
                if (gradient.user_space) {
                    if (const auto parsed = parse_svg_length(val_str)) {
                        out = *parsed;
                        return true;
                    }
                }
                return parse_svg_scalar(val_str, 0.0f, out);
            };
            if (gradient.radial) {
                if (!coordinate("cx", "50%", gradient.p0.x) ||
                    !coordinate("cy", "50%", gradient.p0.y) ||
                    !coordinate("r", "50%", gradient.radius) || gradient.radius <= 0.0f)
                    return "SVG radial gradient coordinates are invalid";
            } else if (!coordinate("x1", "0%", gradient.p0.x) ||
                       !coordinate("y1", "0%", gradient.p0.y) ||
                       !coordinate("x2", "100%", gradient.p1.x) ||
                       !coordinate("y2", "0%", gradient.p1.y)) {
                return "SVG linear gradient coordinates are invalid";
            }
            float previous = 0.0f;
            for (const auto& [child_name, stop] : node) {
                if (child_name != "stop") continue;
                if (gradient.stops.size() >= 64) return "SVG gradient exceeds 64 stops";
                float offset = 0.0f;
                if (!parse_svg_scalar(stop.get<std::string>("<xmlattr>.offset", "0"), 0.0f, offset) || offset < previous || offset < 0.0f || offset > 1.0f)
                    return "SVG gradient stop offsets must be ordered in [0,1]";
                const auto props = style_properties(stop);
                Color stop_current_color = current_color;
                if (const auto color_it = props.find("color"); color_it != props.end()) {
                    if (const auto color = parse_svg_color(color_it->second, stop_current_color))
                        stop_current_color = *color;
                }
                const auto color_it = props.find("stop-color");
                const auto color = parse_svg_color(color_it == props.end() ? "black" : color_it->second, stop_current_color);
                float opacity = 1.0f;
                const auto opacity_it = props.find("stop-opacity");
                if (opacity_it != props.end() && !parse_svg_scalar(opacity_it->second, 1.0f, opacity))
                    return "SVG stop-opacity is invalid";
                if (!color || opacity < 0.0f || opacity > 1.0f) return "SVG gradient stop paint is invalid";
                auto stop_color = *color;
                stop_color.a *= opacity;
                const std::size_t stop_index = gradient.stops.size();
                std::set<std::string> animated_properties;
                for (const auto& [stop_child_name, animation] : stop) {
                    if (stop_child_name != "animate") continue;
                    const auto property = animation.get<std::string>("<xmlattr>.attributeName", "");
                    if (!animated_properties.insert(property).second || gradient.animations.size() >= 64)
                        return "SVG gradient stop animation targets must be unique and bounded";
                    if (animation.get<std::string>("<xmlattr>.repeatCount", "1") != "1" ||
                        animation.get<std::string>("<xmlattr>.fill", "freeze") != "freeze" ||
                        animation.get<std::string>("<xmlattr>.additive", "replace") != "replace" ||
                        animation.get<std::string>("<xmlattr>.begin", "0s") != "0s")
                        return "SVG gradient stop animations require one iteration, freeze, and begin=0";
                    float duration = 0.0f;
                    auto duration_text = animation.get<std::string>("<xmlattr>.dur", "");
                    float time_scale = 1.0f;
                    if (duration_text.ends_with("ms")) { time_scale = 0.001f; duration_text.resize(duration_text.size() - 2); }
                    else if (duration_text.ends_with("s")) duration_text.pop_back();
                    try {
                        std::size_t used = 0;
                        duration = std::stof(duration_text, &used) * time_scale;
                        if (used != duration_text.size() || !std::isfinite(duration) || duration <= 0.0f || duration > 3600.0f)
                            return "SVG gradient animation duration is invalid";
                    } catch (const std::invalid_argument&) { return "SVG gradient animation duration is invalid"; } catch (const std::out_of_range&) { return "SVG gradient animation duration is invalid"; }
                    const auto mode = animation.get<std::string>("<xmlattr>.calcMode", "linear");
                    if (mode != "linear" && mode != "discrete") return "SVG gradient animation calcMode is unsupported";
                    std::string from_text = animation.get<std::string>("<xmlattr>.from", "");
                    std::string to_text = animation.get<std::string>("<xmlattr>.to", "");
                    if (const auto values = animation.get_optional<std::string>("<xmlattr>.values")) {
                        const auto separator = values->find(';');
                        if (separator == std::string::npos || values->find(';', separator + 1) != std::string::npos)
                            return "SVG gradient animation supports exactly two values";
                        from_text = values->substr(0, separator);
                        to_text = values->substr(separator + 1);
                    }
                    SvgGradientSpec::StopAnimation track;
                    track.stop_index = stop_index;
                    track.duration_seconds = duration;
                    track.discrete = mode == "discrete";
                    if (property == "stop-color") {
                        const auto from_color = parse_svg_color(from_text, stop_current_color);
                        const auto to_color = parse_svg_color(to_text, stop_current_color);
                        if (!from_color || !to_color) return "SVG animated stop-color values are invalid";
                        track.from = *from_color;
                        track.to = *to_color;
                    } else if (property == "stop-opacity") {
                        if (!parse_svg_scalar(from_text, 0.0f, track.from.a) ||
                            !parse_svg_scalar(to_text, 0.0f, track.to.a) ||
                            track.from.a < 0.0f || track.from.a > 1.0f ||
                            track.to.a < 0.0f || track.to.a > 1.0f)
                            return "SVG animated stop-opacity values must be in [0,1]";
                        track.opacity = true;
                    } else {
                        return "SVG gradient animation property is unsupported";
                    }
                    gradient.animations.push_back(track);
                }
                gradient.stops.push_back({offset, stop_color});
                previous = offset;
            }
            if (gradient.stops.size() < 2) return "SVG gradients require at least two stops";
            if (!gradient.animations.empty()) {
                const auto& reference = gradient.animations.front();
                for (const auto& animation : gradient.animations)
                    if (std::abs(animation.duration_seconds - reference.duration_seconds) > 1e-6f ||
                        animation.discrete != reference.discrete)
                        return "SVG animated gradient stops must share duration and calcMode";
            }
            gradients.emplace(*id, std::move(gradient));
        } else if (name != "<xmlattr>") {
            if (auto error = collect_svg_gradients_impl(node, gradients, patterns, current_color)) return error;
        }
    }
    return std::nullopt;
}

std::optional<std::string> collect_svg_gradients(
    const boost::property_tree::ptree& tree, SvgGradientMap& gradients,
    Color inherited_color) {
    SvgPatternMap patterns;
    if (auto error = collect_svg_gradients_impl(
            tree, gradients, patterns, inherited_color))
        return error;
    return import_svg_patterns(patterns, gradients);
}

}  // namespace chronon3d::assets::svg_gradient_detail
