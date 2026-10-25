#include "svg_document_validation.hpp"
#include "svg_path_loader_detail.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::assets::svg_document_detail {
using svg_loader_detail::parse_svg_color;
using svg_loader_detail::parse_svg_length;
using svg_loader_detail::trim;

std::optional<std::string> parse_root_svg_filter(
    const boost::property_tree::ptree& filter,
    std::vector<SvgFilterEffectSpec>& effects) {
    if (filter.get<std::string>("<xmlattr>.filterUnits", "objectBoundingBox") !=
            "objectBoundingBox" ||
        filter.get<std::string>("<xmlattr>.primitiveUnits", "userSpaceOnUse") !=
            "userSpaceOnUse")
        return "SVG filter supports objectBoundingBox filterUnits and userSpaceOnUse primitiveUnits";
    if (const auto attributes = filter.get_child_optional("<xmlattr>")) {
        for (const auto& [key, value] : *attributes) {
            (void)value;
            if (key != "id" && key != "filterUnits" && key != "primitiveUnits")
                return "SVG filter contains an unsupported attribute: " + key;
        }
    }
    for (const auto* key : {"filterRes"})
        if (filter.get_optional<std::string>(std::string("<xmlattr>.") + key))
            return "SVG filter does not support custom filter regions or filterRes";
    effects.clear();
    std::string previous_result;
    std::set<std::string> result_names;
    bool previous_is_blend = false;
    std::size_t primitive_count = 0;
    auto parse_deviation = [](std::string text, float& deviation) -> bool {
        std::replace(text.begin(), text.end(), ',', ' ');
        std::istringstream values(text);
        std::vector<float> parsed;
        std::string token;
        while (values >> token) {
            try {
                std::size_t used = 0;
                const float value = std::stof(token, &used);
                if (used != token.size() || !std::isfinite(value) || value < 0.0f || value > 256.0f)
                    return false;
                parsed.push_back(value);
                if (parsed.size() > 2) return false;
            } catch (const std::exception&) { return false; }
        }
        if (parsed.empty() || (parsed.size() == 2 && std::abs(parsed[0] - parsed[1]) > 1e-6f))
            return false;
        deviation = parsed.front();
        return true;
    };
    const auto only_attributes = [](const boost::property_tree::ptree& node,
                                    std::initializer_list<std::string_view> allowed) {
        const auto attributes = node.get_child_optional("<xmlattr>");
        if (!attributes) return true;
        for (const auto& [key, value] : *attributes) {
            (void)value;
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) return false;
        }
        return true;
    };
    for (const auto& [name, child] : filter) {
        if (name == "<xmlattr>" || name == "<xmlcomment>" || name == "<xmltext>") {
            if (name == "<xmltext>" && !trim(child.data()).empty())
                return "SVG filter definitions cannot contain text";
            continue;
        }
        if (++primitive_count > 8) return "SVG filter exceeds the eight primitive limit";
        if (previous_is_blend)
            return "SVG feBlend must be the final primitive in the root filter";
        if (name != "feGaussianBlur" && name != "feDropShadow" && name != "feColorMatrix" && name != "feBlend")
            return "SVG filter supports only feGaussianBlur, feDropShadow, feColorMatrix, and final feBlend in a linear chain";
        const auto input = child.get_optional<std::string>("<xmlattr>.in");
        if ((primitive_count == 1 && input.value_or("SourceGraphic") != "SourceGraphic") ||
            (primitive_count > 1 && input && (previous_result.empty() || *input != previous_result)))
            return "SVG filter primitives must consume SourceGraphic first and then the previous named result";
        SvgFilterEffectSpec effect;
        if (name == "feGaussianBlur") {
            effect.kind = SvgFilterEffectSpec::Kind::GaussianBlur;
            if (child.get<std::string>("<xmlattr>.edgeMode", "none") != "none" ||
                !only_attributes(child, {"stdDeviation", "in", "edgeMode", "result"}) ||
                !parse_deviation(child.get<std::string>("<xmlattr>.stdDeviation", "0"), effect.radius))
                return "SVG feGaussianBlur requires isotropic stdDeviation in [0,256] and edgeMode=none";
        } else if (name == "feDropShadow") {
            effect.kind = SvgFilterEffectSpec::Kind::DropShadow;
            const auto dx = parse_svg_length(child.get<std::string>("<xmlattr>.dx", "2"));
            const auto dy = parse_svg_length(child.get<std::string>("<xmlattr>.dy", "2"));
            const auto color = parse_svg_color(child.get<std::string>("<xmlattr>.flood-color", "black"), Color::black());
            float opacity = 1.0f;
            try {
                const auto raw = child.get<std::string>("<xmlattr>.flood-opacity", "1");
                std::size_t used = 0; opacity = std::stof(raw, &used);
                if (used != raw.size()) return "SVG feDropShadow flood-opacity is invalid";
            } catch (const std::exception&) { return "SVG feDropShadow flood-opacity is invalid"; }
            if (!dx || !dy || !color || !std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f ||
                std::abs(*dx) > 10000.0f || std::abs(*dy) > 10000.0f ||
                !parse_deviation(child.get<std::string>("<xmlattr>.stdDeviation", "2"), effect.radius) ||
                !only_attributes(child, {"dx", "dy", "stdDeviation", "in", "result", "flood-color", "flood-opacity"}))
                return "SVG feDropShadow has unsupported or out-of-range parameters";
            effect.offset = {*dx, *dy};
            effect.color = {color->r, color->g, color->b, color->a * opacity};
        } else if (name == "feColorMatrix") {
            effect.kind = SvgFilterEffectSpec::Kind::ColorMatrix;
            if (child.get<std::string>("<xmlattr>.type", "matrix") != "matrix" ||
                !only_attributes(child, {"type", "values", "in", "result"}))
                return "SVG feColorMatrix supports only type=matrix";
            const auto values_text = child.get_optional<std::string>("<xmlattr>.values");
            if (!values_text)
                return "SVG feColorMatrix type=matrix requires its values attribute";
            auto matrix_text = *values_text;
            std::replace(matrix_text.begin(), matrix_text.end(), ',', ' ');
            std::istringstream values(matrix_text);
            for (float& coefficient : effect.color_matrix) {
                if (!(values >> coefficient) || !std::isfinite(coefficient) ||
                    std::abs(coefficient) > 16.0f)
                    return "SVG feColorMatrix requires exactly 20 finite coefficients in [-16,16]";
            }
            std::string extra;
            if (values >> extra) return "SVG feColorMatrix requires exactly 20 coefficients";
        } else {
            effect.kind = SvgFilterEffectSpec::Kind::Blend;
            effect.blend_mode = child.get<std::string>("<xmlattr>.mode", "normal");
            if (child.get<std::string>("<xmlattr>.in2", "") != "BackgroundImage" ||
                !only_attributes(child, {"in", "in2", "mode", "result"}))
                return "SVG feBlend supports only a BackgroundImage second input";
            static const std::set<std::string> blend_modes{
                "normal", "multiply", "screen", "overlay", "darken", "lighten",
                "difference", "exclusion", "soft-light", "hard-light"};
            if (!blend_modes.contains(effect.blend_mode))
                return "SVG feBlend mode is outside the software compositor subset";
            previous_is_blend = true;
        }
        for (const auto& [child_name, grandchild] : child) {
            if (child_name != "<xmlattr>" && child_name != "<xmlcomment>" && child_name != "<xmltext>")
                return "SVG filter primitives cannot contain child elements";
            if (child_name == "<xmltext>" && !trim(grandchild.data()).empty())
                return "SVG filter primitives cannot contain text";
        }
        const auto result = child.get<std::string>("<xmlattr>.result", "");
        if (result.size() > 128)
            return "SVG filter result names cannot exceed 128 characters";
        if (!result.empty()) {
            if (!result_names.insert(result).second)
                return "SVG filter contains duplicate results";
            previous_result = result;
        } else previous_result.clear();
        effects.push_back(effect);
    }
    if (effects.empty()) return "SVG filter must contain at least one supported primitive";
    return std::nullopt;
}

} // namespace chronon3d::assets::svg_document_detail
