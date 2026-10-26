#include "svg_animation_detail.hpp"

#include "svg_gradient_detail.hpp"
#include "svg_importer.hpp"
#include "svg_path_loader_detail.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::assets::svg_animation_detail {
using svg_loader_detail::parse_svg_color;
using svg_loader_detail::transform_numbers;
using svg_loader_detail::trim;
using svg_gradient_detail::parse_svg_scalar;

bool parse_svg_animation(const std::string& tag, const boost::property_tree::ptree& node,
                         SvgAnimationSpec& output) {
    const auto parse_time = [](std::string text, float fallback, float& value) {
        if (text.empty()) { value = fallback; return true; }
        float scale = 1.0f;
        if (text.ends_with("ms")) { scale = 0.001f; text.resize(text.size() - 2); }
        else if (text.ends_with("s")) text.pop_back();
        try {
            std::size_t used = 0;
            value = std::stof(text, &used) * scale;
            return used == text.size() && std::isfinite(value) && value >= 0.0f && value <= 3600.0f;
        } catch (const std::invalid_argument&) { return false; } catch (const std::out_of_range&) { return false; }
    };
    const auto parse_values = [](const std::string& text, std::vector<float>& values) {
        if (text.empty()) return false;
        for (const char c : text) {
            if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' ||
                  c == 'e' || c == 'E' || c == ',' || std::isspace(static_cast<unsigned char>(c))))
                return false;
        }
        values = transform_numbers(text);
        return !values.empty() && values.size() <= 3 &&
            std::all_of(values.begin(), values.end(), [](float value) {
                return std::isfinite(value) && std::abs(value) <= 100000.0f;
            });
    };
    const bool transform = tag == "animateTransform";
    const auto attribute = node.get_optional<std::string>("<xmlattr>.attributeName");
    if (!attribute || (transform && *attribute != "transform")) return false;
    SvgAnimationKind paint_kind = SvgAnimationKind::Opacity;
    bool color_property = false;
    bool opacity_property = false;
    if (!transform) {
        if (*attribute == "opacity") paint_kind = SvgAnimationKind::Opacity;
        else if (*attribute == "fill") { paint_kind = SvgAnimationKind::FillColor; color_property = true; }
        else if (*attribute == "stroke") { paint_kind = SvgAnimationKind::StrokeColor; color_property = true; }
        else if (*attribute == "stroke-width") paint_kind = SvgAnimationKind::StrokeWidth;
        else if (*attribute == "fill-opacity") { paint_kind = SvgAnimationKind::FillOpacity; opacity_property = true; }
        else if (*attribute == "stroke-opacity") { paint_kind = SvgAnimationKind::StrokeOpacity; opacity_property = true; }
        else return false;
    }
    if (node.get<std::string>("<xmlattr>.repeatCount", "1") != "1" ||
        node.get<std::string>("<xmlattr>.fill", "freeze") != "freeze" ||
        node.get<std::string>("<xmlattr>.additive", "replace") != "replace") return false;
    float duration = 0.0f, begin = 0.0f;
    if (!parse_time(node.get<std::string>("<xmlattr>.dur", ""), 0.0f, duration) ||
        !parse_time(node.get<std::string>("<xmlattr>.begin", "0s"), 0.0f, begin) || duration <= 0.0f)
        return false;
    const auto mode = node.get<std::string>("<xmlattr>.calcMode", "linear");
    if (mode != "linear" && mode != "discrete") return false;
    std::vector<float> from, to;
    if (color_property) {
        std::string from_color, to_color;
        if (const auto values = node.get_optional<std::string>("<xmlattr>.values")) {
            const auto separator = values->find(';');
            if (separator == std::string::npos || values->find(';', separator + 1) != std::string::npos) return false;
            from_color = values->substr(0, separator);
            to_color = values->substr(separator + 1);
        } else {
            from_color = node.get<std::string>("<xmlattr>.from", "");
            to_color = node.get<std::string>("<xmlattr>.to", "");
        }
        const auto color_from = parse_svg_color(from_color, Color::black());
        const auto color_to = parse_svg_color(to_color, Color::black());
        if (!color_from || !color_to) return false;
        from = {color_from->r, color_from->g, color_from->b, color_from->a};
        to = {color_to->r, color_to->g, color_to->b, color_to->a};
    } else if (const auto values = node.get_optional<std::string>("<xmlattr>.values")) {
        const auto separator = values->find(';');
        if (separator == std::string::npos || values->find(';', separator + 1) != std::string::npos ||
            !parse_values(values->substr(0, separator), from) ||
            !parse_values(values->substr(separator + 1), to)) return false;
    } else if (!parse_values(node.get<std::string>("<xmlattr>.from", ""), from) ||
               !parse_values(node.get<std::string>("<xmlattr>.to", ""), to)) return false;
    if (transform) {
        const auto type = node.get<std::string>("<xmlattr>.type", "");
        const auto expected = type == "translate" || type == "scale" ? 2U : type == "rotate" ? 1U : 0U;
        if (expected == 0 || from.size() != to.size() ||
            (type == "rotate" ? (from.size() != 1 && from.size() != 3)
                               : from.size() != expected)) return false;
        if (type == "scale" && (std::abs(from[0] - from[1]) > 1e-6f ||
                                 std::abs(to[0] - to[1]) > 1e-6f)) return false;
        if (type == "rotate" && from.size() == 3) {
            if (std::abs(from[1] - to[1]) > 1e-6f ||
                std::abs(from[2] - to[2]) > 1e-6f) return false;
            output.pivot = {from[1], from[2]};
            output.has_pivot = true;
        }
        output.kind = type == "translate" ? SvgAnimationKind::Translate :
            type == "scale" ? SvgAnimationKind::Scale : SvgAnimationKind::Rotate;
        output.components = static_cast<std::uint8_t>(expected);
    } else if (!color_property) {
        if (from.size() != 1 || to.size() != 1 || !std::isfinite(from[0]) || !std::isfinite(to[0])) return false;
        if ((opacity_property || *attribute == "opacity") &&
            (from[0] < 0.0f || from[0] > 1.0f || to[0] < 0.0f || to[0] > 1.0f)) return false;
        if (*attribute == "stroke-width" && (from[0] < 0.0f || to[0] < 0.0f || from[0] > 100000.0f || to[0] > 100000.0f)) return false;
        output.kind = paint_kind;
        output.components = 1;
    }
    if (color_property) output.components = 4;
    else if (transform) output.components = static_cast<std::uint8_t>(
        output.kind == SvgAnimationKind::Rotate && output.has_pivot ? 1 : from.size());
    output.kind = transform ? output.kind : paint_kind;
    for (std::size_t i = 0; i < from.size() && i < 4; ++i) output.from[i] = from[i];
    for (std::size_t i = 0; i < to.size() && i < 4; ++i) output.to[i] = to[i];
    output.begin_seconds = begin;
    output.duration_seconds = duration;
    output.discrete = mode == "discrete";
    return true;
}

bool parse_svg_morph_animation(const boost::property_tree::ptree& node,
                               SvgMorphAnimationSpec& output) {
    if (node.get<std::string>("<xmlattr>.attributeName", "") != "d" ||
        node.get<std::string>("<xmlattr>.repeatCount", "1") != "1" ||
        node.get<std::string>("<xmlattr>.fill", "freeze") != "freeze" ||
        node.get<std::string>("<xmlattr>.additive", "replace") != "replace") return false;
    const auto parse_time = [](std::string text, float& seconds) {
        float scale = 1.0f;
        if (text.ends_with("ms")) { scale = 0.001f; text.resize(text.size() - 2); }
        else if (text.ends_with("s")) text.pop_back();
        try {
            std::size_t used = 0;
            seconds = std::stof(text, &used) * scale;
            return used == text.size() && std::isfinite(seconds) && seconds >= 0.0f && seconds <= 3600.0f;
        } catch (const std::invalid_argument&) { return false; } catch (const std::out_of_range&) { return false; }
    };
    float begin = 0.0f;
    if (!parse_time(node.get<std::string>("<xmlattr>.begin", "0s"), begin) || begin != 0.0f ||
        !parse_time(node.get<std::string>("<xmlattr>.dur", ""), output.duration_seconds) ||
        output.duration_seconds <= 0.0f) return false;
    const auto mode = node.get<std::string>("<xmlattr>.calcMode", "linear");
    if (mode != "linear" && mode != "discrete") return false;
    const auto values = node.get_optional<std::string>("<xmlattr>.values");
    if (!values) return false;
    std::vector<std::string> path_values;
    std::size_t start = 0;
    while (start <= values->size()) {
        const auto separator = values->find(';', start);
        const auto end = separator == std::string::npos ? values->size() : separator;
        auto value = trim(values->substr(start, end - start));
        if (value.empty() || path_values.size() >= 256) return false;
        path_values.push_back(std::move(value));
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    if (path_values.size() < 2) return false;
    output.key_times.resize(path_values.size());
    if (const auto key_times = node.get_optional<std::string>("<xmlattr>.keyTimes")) {
        std::vector<float> parsed;
        std::istringstream input(*key_times);
        std::string item;
        while (std::getline(input, item, ';')) {
            float time = 0.0f;
            if (!parse_svg_scalar(item, 0.0f, time) || time < 0.0f || time > 1.0f) return false;
            parsed.push_back(time);
        }
        if (parsed.size() != path_values.size() || parsed.front() != 0.0f || parsed.back() != 1.0f)
            return false;
        for (std::size_t index = 1; index < parsed.size(); ++index)
            if (!(parsed[index] > parsed[index - 1])) return false;
        output.key_times = std::move(parsed);
    } else {
        for (std::size_t index = 0; index < path_values.size(); ++index)
            output.key_times[index] = static_cast<float>(index) / static_cast<float>(path_values.size() - 1);
    }
    output.paths.clear();
    output.paths.reserve(path_values.size());
    for (const auto& value : path_values) {
        auto imported = SvgImporter{}.import_path_data(value, {});
        if (!imported.ok) return false;
        output.paths.push_back(std::move(imported.path));
    }
    output.discrete = mode == "discrete";
    return true;
}


}  // namespace chronon3d::assets::svg_animation_detail
