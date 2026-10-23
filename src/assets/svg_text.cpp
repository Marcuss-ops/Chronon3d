#include "svg_text_detail.hpp"
#include "svg_importer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::assets::svg_text_detail {
using svg_gradient_detail::Affine2;
using svg_gradient_detail::SvgGradientMap;
using svg_gradient_detail::apply_transform;
using svg_gradient_detail::parse_transform;
using svg_gradient_detail::parse_viewbox_transform;
using svg_gradient_detail::parse_svg_gradient_reference;
using svg_gradient_detail::transform_point;
using svg_gradient_detail::parse_svg_scalar;
using svg_loader_detail::parse_svg_color;
using svg_loader_detail::parse_svg_length;
using svg_loader_detail::style_properties;
using svg_loader_detail::style_number;
using svg_loader_detail::transform_numbers;
using svg_loader_detail::trim;
std::optional<float> svg_baseline_shift(std::string value, float font_size) {
    value = trim(std::move(value));
    if (value == "baseline" || value == "0" || value == "0px") return 0.0f;
    if (value == "sub") return font_size * 0.2f;
    if (value == "super") return -font_size * 0.33f;
    const bool percent = value.ends_with('%');
    const bool em = value.ends_with("em");
    const bool px = value.ends_with("px");
    if (percent) value.pop_back();
    else if (em) value.resize(value.size() - 2);
    else if (px) value.resize(value.size() - 2);
    try {
        std::size_t used = 0;
        float amount = std::stof(value, &used);
        if (used != value.size() || !std::isfinite(amount) || std::abs(amount) > 100000.0f)
            return std::nullopt;
        if (percent) amount = amount * font_size / 100.0f;
        else if (em) amount *= font_size;
        return -amount;
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

std::optional<std::string> svg_text_path_reference(
    const boost::property_tree::ptree& node);
std::optional<std::pair<float, bool>> svg_text_path_offset(
    const boost::property_tree::ptree& node);









std::vector<float> svg_text_lengths(const boost::property_tree::ptree& node,
                                    const char* key) {
    auto value = node.get_optional<std::string>(std::string("<xmlattr>.") + key);
    if (!value) return {};
    std::string list = *value;
    std::replace(list.begin(), list.end(), ',', ' ');
    std::istringstream input(list);
    std::vector<float> result;
    std::string token;
    while (input >> token) {
        if (token.ends_with("px")) token.resize(token.size() - 2);
        try {
            std::size_t used = 0;
            const float parsed = std::stof(token, &used);
            if (used != token.size() || !std::isfinite(parsed) || std::abs(parsed) > 100000.0f)
                throw std::invalid_argument("invalid SVG text coordinate");
            result.push_back(parsed);
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument(std::string("SVG text ") + key +
                " must contain finite bounded pixel values");
        } catch (const std::out_of_range&) {
            throw std::invalid_argument(std::string("SVG text ") + key +
                " must contain finite bounded pixel values");
        }
        if (result.size() > 4096)
            throw std::invalid_argument(std::string("SVG text ") + key +
                " exceeds the 4096-coordinate limit");
    }
    if (result.empty())
        throw std::invalid_argument(std::string("SVG text ") + key + " coordinate list is empty");
    return result;
}

std::vector<float> svg_text_angles(const boost::property_tree::ptree& node) {
    const auto value = node.get_optional<std::string>("<xmlattr>.rotate");
    if (!value) return {};
    std::string list = *value;
    std::replace(list.begin(), list.end(), ',', ' ');
    std::istringstream input(list);
    std::vector<float> result;
    std::string token;
    while (input >> token) {
        if (token.ends_with("deg")) token.resize(token.size() - 3);
        try {
            std::size_t used = 0;
            const float parsed = std::stof(token, &used);
            if (used != token.size() || !std::isfinite(parsed) || std::abs(parsed) > 36000.0f)
                throw std::invalid_argument("invalid SVG text rotate angle");
            result.push_back(parsed);
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument("SVG text rotate values must be bounded degrees");
        } catch (const std::out_of_range&) {
            throw std::invalid_argument("SVG text rotate values must be bounded degrees");
        }
        if (result.size() > 4096)
            throw std::invalid_argument("SVG text rotate list exceeds the 4096-angle limit");
    }
    if (result.empty())
        throw std::invalid_argument("SVG text rotate list is empty");
    return result;
}

float svg_text_spacing(const std::string& source, float font_size) {
    auto value = trim(source);
    if (value == "normal") return 0.0f;
    float factor = 1.0f;
    if (value.ends_with("px")) value.resize(value.size() - 2);
    else if (value.ends_with("em")) {
        value.resize(value.size() - 2);
        factor = font_size;
    }
    try {
        std::size_t used = 0;
        const float parsed = std::stof(value, &used);
        if (used != value.size() || !std::isfinite(parsed) || std::abs(parsed) > 100000.0f)
            throw std::invalid_argument("invalid SVG text spacing");
        return parsed * factor;
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("SVG text spacing must be a finite bounded px or em value");
    } catch (const std::out_of_range&) {
        throw std::invalid_argument("SVG text spacing must be a finite bounded px or em value");
    }
}

std::optional<float> svg_text_length(const boost::property_tree::ptree& node,
                                     const char* key) {
    const auto values = svg_text_lengths(node, key);
    if (values.empty()) return std::nullopt;
    if (values.size() != 1)
        throw std::invalid_argument(std::string("SVG text ") + key +
            " must contain exactly one length");
    return values.front();
}

std::optional<std::string> svg_text_path_reference(
    const boost::property_tree::ptree& node) {
    auto href = node.get_optional<std::string>("<xmlattr>.href");
    if (!href) href = node.get_optional<std::string>("<xmlattr>.xlink:href");
    if (!href) href = node.get_optional<std::string>(
        "<xmlattr>.{http://www.w3.org/1999/xlink}href");
    if (!href || href->size() < 2 || href->front() != '#' || href->size() > 129)
        return std::nullopt;
    const auto id = href->substr(1);
    if (id.find_first_of(" \t\r\n#") != std::string::npos) return std::nullopt;
    return id;
}

std::optional<std::pair<float, bool>> svg_text_path_offset(
    const boost::property_tree::ptree& node) {
    const auto raw = node.get_optional<std::string>("<xmlattr>.startOffset");
    if (!raw) return std::pair{0.0f, false};
    auto value = trim(*raw);
    const bool percent = value.ends_with('%');
    if (percent) value.pop_back();
    try {
        std::size_t used = 0;
        const float parsed = std::stof(value, &used);
        if (used != value.size() || !std::isfinite(parsed) || std::abs(parsed) > 100000.0f)
            return std::nullopt;
        return std::pair{parsed, percent};
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

std::string svg_font_family(const std::string& authored) {
    auto value = trim(authored);
    const auto comma = value.find(',');
    if (comma != std::string::npos) value = trim(value.substr(0, comma));
    if (value.size() >= 2 &&
        ((value.front() == '\'' && value.back() == '\'') ||
         (value.front() == '"' && value.back() == '"')))
        value = value.substr(1, value.size() - 2);
    return value;
}

std::string svg_color_hex(const Color& color) {
    std::ostringstream out;
    out << '#' << std::uppercase << std::hex << std::setfill('0')
        << std::setw(2) << static_cast<int>(std::round(std::clamp(color.r, 0.0f, 1.0f) * 255.0f))
        << std::setw(2) << static_cast<int>(std::round(std::clamp(color.g, 0.0f, 1.0f) * 255.0f))
        << std::setw(2) << static_cast<int>(std::round(std::clamp(color.b, 0.0f, 1.0f) * 255.0f));
    return out.str();
}

void apply_svg_text_style(const boost::property_tree::ptree& node,
                          SvgTextStyleState& state,
                          const SvgGradientMap& gradients) {
    const auto properties = style_properties(node);
    if (const auto it = properties.find("display"); it != properties.end()) {
        auto value = trim(it->second);
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        state.display_none = state.display_none || value == "none";
    }
    if (const auto it = properties.find("visibility"); it != properties.end()) {
        auto value = trim(it->second);
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        state.visibility_hidden = value == "hidden" || value == "collapse";
    }
    if (const auto it = properties.find("font-family"); it != properties.end()) {
        state.font_family = svg_font_family(it->second);
        if (state.font_family.empty() || state.font_family.size() > 256)
            throw std::invalid_argument("SVG font-family is empty or exceeds 256 bytes");
    }
    if (const auto it = properties.find("font-size"); it != properties.end()) {
        auto value = trim(it->second);
        if (value.ends_with("px")) value.resize(value.size() - 2);
        state.font_size = std::stof(value);
    }
    if (const auto it = properties.find("letter-spacing"); it != properties.end())
        state.letter_spacing = svg_text_spacing(it->second, state.font_size);
    if (const auto it = properties.find("word-spacing"); it != properties.end())
        state.word_spacing = svg_text_spacing(it->second, state.font_size);
    if (node.get_optional<std::string>("<xmlattr>.rotate"))
        state.rotate_degrees = svg_text_angles(node);
    if (node.get_optional<std::string>("<xmlattr>.textLength")) {
        state.text_length = svg_text_length(node, "textLength");
        if (!state.text_length || *state.text_length <= 0.0f)
            throw std::invalid_argument("SVG textLength must be a positive finite pixel value");
        state.length_adjust = node.get<std::string>("<xmlattr>.lengthAdjust", "spacing");
        if (state.length_adjust != "spacing" && state.length_adjust != "spacingAndGlyphs")
            throw std::invalid_argument("SVG lengthAdjust supports spacing or spacingAndGlyphs");
    }
    if (const auto it = properties.find("font-weight"); it != properties.end()) {
        if (it->second == "normal") state.font_weight = 400;
        else if (it->second == "bold") state.font_weight = 700;
        else state.font_weight = std::stoi(it->second);
    }
    if (const auto it = properties.find("font-style"); it != properties.end())
        state.font_style = it->second;
    if (const auto it = properties.find("direction"); it != properties.end())
        state.direction = it->second;
    if (const auto it = properties.find("writing-mode"); it != properties.end() &&
        it->second != "horizontal-tb" && it->second != "lr-tb")
        throw std::invalid_argument("vertical SVG text writing modes are not supported by the bounded importer");
    if (const auto it = properties.find("text-anchor"); it != properties.end())
        state.text_anchor = it->second;
    if (const auto it = properties.find("dominant-baseline"); it != properties.end() &&
        it->second != "no-change" && it->second != "reset-size")
        state.dominant_baseline = it->second;
    if (const auto it = properties.find("color"); it != properties.end())
        if (const auto color = parse_svg_color(it->second, state.current_color))
            state.current_color = *color;
    if (const auto it = properties.find("fill"); it != properties.end()) {
        std::string gradient_id;
        const bool gradient_reference = parse_svg_gradient_reference(it->second, gradient_id);
        if (trim(it->second).starts_with("url(") && !gradient_reference)
            throw std::invalid_argument("SVG text fill requires a local gradient or supported pattern reference");
        if (gradient_reference) {
            const auto gradient = gradients.find(gradient_id);
            if (gradient == gradients.end())
                throw std::invalid_argument("SVG text fill references an unknown gradient");
            const auto& source = gradient->second;
            if (source.pattern_tile) {
                state.fill_pattern = source.pattern_tile;
                state.fill_gradient.reset();
            } else {
            if (!source.animations.empty())
                throw std::invalid_argument("animated gradient stops in SVG text paint are not supported");
            const Vec2 origin = transform_point(source.transform, {0.0f, 0.0f});
            const Vec2 axis_x = transform_point(source.transform, {1.0f, 0.0f}) - origin;
            const Vec2 axis_y = transform_point(source.transform, {0.0f, 1.0f}) - origin;
            if (source.radial) {
                const float sx = glm::length(axis_x), sy = glm::length(axis_y);
                if (sx <= 0.0f || sy <= 0.0f ||
                    std::abs(sx - sy) > 1.0e-5f * std::max(sx, sy) ||
                    std::abs(glm::dot(axis_x, axis_y)) > 1.0e-5f * sx * sy)
                    throw std::invalid_argument("SVG text radial gradientTransform must preserve circular geometry");
                state.fill_gradient = graphics::GradientDefinition::radial(
                    transform_point(source.transform, source.p0), source.radius * sx,
                    source.stops);
            } else {
                state.fill_gradient = graphics::GradientDefinition::linear(
                    transform_point(source.transform, source.p0),
                    transform_point(source.transform, source.p1), source.stops);
            }
            state.fill_pattern.reset();
            }
        } else if (const auto color = parse_svg_color(it->second, state.current_color)) {
            state.fill_gradient.reset();
            state.fill_pattern.reset();
            state.fill = *color;
        } else {
            state.fill_gradient.reset();
            state.fill_pattern.reset();
            state.fill.a = 0.0f;
        }
    }
    if (const auto it = properties.find("stroke"); it != properties.end()) {
        std::string gradient_id;
        const bool gradient_reference = parse_svg_gradient_reference(it->second, gradient_id);
        if (trim(it->second).starts_with("url(") && !gradient_reference)
            throw std::invalid_argument("SVG text stroke requires a local gradient or supported pattern reference");
        if (gradient_reference) {
            const auto gradient = gradients.find(gradient_id);
            if (gradient == gradients.end())
                throw std::invalid_argument("SVG text stroke references an unknown gradient");
            const auto& source = gradient->second;
            if (source.pattern_tile) {
                state.stroke_pattern = source.pattern_tile;
                state.stroke_gradient.reset();
                state.stroke = Color::black();
            } else {
            if (!source.animations.empty())
                throw std::invalid_argument("animated gradient stops in SVG text paint are not supported");
            const Vec2 origin = transform_point(source.transform, {0.0f, 0.0f});
            const Vec2 axis_x = transform_point(source.transform, {1.0f, 0.0f}) - origin;
            const Vec2 axis_y = transform_point(source.transform, {0.0f, 1.0f}) - origin;
            if (source.radial) {
                const float sx = glm::length(axis_x), sy = glm::length(axis_y);
                if (sx <= 0.0f || sy <= 0.0f ||
                    std::abs(sx - sy) > 1.0e-5f * std::max(sx, sy) ||
                    std::abs(glm::dot(axis_x, axis_y)) > 1.0e-5f * sx * sy)
                    throw std::invalid_argument("SVG text radial stroke gradientTransform must preserve circular geometry");
                state.stroke_gradient = graphics::GradientDefinition::radial(
                    transform_point(source.transform, source.p0), source.radius * sx,
                    source.stops);
            } else {
                state.stroke_gradient = graphics::GradientDefinition::linear(
                    transform_point(source.transform, source.p0),
                    transform_point(source.transform, source.p1), source.stops);
            }
            state.stroke = Color::black();
            state.stroke_pattern.reset();
            }
        } else if (trim(it->second) == "none") {
            state.stroke_gradient.reset();
            state.stroke_pattern.reset();
            state.stroke.reset();
        } else if (const auto color = parse_svg_color(it->second, state.current_color)) {
            state.stroke_gradient.reset();
            state.stroke_pattern.reset();
            state.stroke = *color;
        } else {
            throw std::invalid_argument("SVG text stroke must be a solid color or none");
        }
    }
    if (const auto it = properties.find("stroke-width"); it != properties.end()) {
        auto value = trim(it->second);
        if (value.ends_with("px")) value.resize(value.size() - 2);
        try {
            std::size_t used = 0;
            const float width = std::stof(value, &used);
            if (used != value.size() || !std::isfinite(width) || width < 0.0f || width > 100000.0f)
                throw std::invalid_argument("invalid stroke width");
            state.stroke_width = width;
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument("SVG text stroke-width must be a finite bounded pixel value");
        } catch (const std::out_of_range&) {
            throw std::invalid_argument("SVG text stroke-width must be a finite bounded pixel value");
        }
    }
    if (const auto it = properties.find("stroke-linecap"); it != properties.end()) {
        state.stroke_cap = it->second == "round" ? LineCap::Round
            : it->second == "square" ? LineCap::Square : LineCap::Butt;
    }
    if (const auto it = properties.find("stroke-linejoin"); it != properties.end()) {
        state.stroke_join = it->second == "round" ? LineJoin::Round
            : it->second == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
    }
    if (const auto it = properties.find("stroke-miterlimit"); it != properties.end()) {
        state.stroke_miter_limit = std::stof(it->second);
    }
    if (const auto it = properties.find("stroke-dasharray"); it != properties.end()) {
        state.stroke_dash_array = trim(it->second) == "none"
            ? std::vector<float>{} : transform_numbers(it->second);
        if (state.stroke_dash_array.size() % 2 != 0) {
            const auto original = state.stroke_dash_array;
            state.stroke_dash_array.insert(state.stroke_dash_array.end(),
                original.begin(), original.end());
        }
    }
    if (const auto it = properties.find("stroke-dashoffset"); it != properties.end())
        state.stroke_dash_offset = std::stof(it->second);
    if (const auto it = properties.find("baseline-shift"); it != properties.end()) {
        const auto shift = svg_baseline_shift(it->second, state.font_size);
        if (!shift) throw std::invalid_argument("SVG baseline-shift supports baseline, sub, super, px, em, or percent values");
        state.baseline_shift += *shift;
    }
    if (node.get_optional<std::string>("<xmlattr>.href") ||
        node.get_optional<std::string>("<xmlattr>.xlink:href") ||
        node.get_optional<std::string>(
            "<xmlattr>.{http://www.w3.org/1999/xlink}href")) {
        const auto reference = svg_text_path_reference(node);
        const auto offset = svg_text_path_offset(node);
        if (!reference || !offset)
            throw std::invalid_argument("SVG textPath requires a local href and valid startOffset");
        state.text_path_id = *reference;
        state.text_path_start_offset = offset->first;
        state.text_path_start_offset_percent = offset->second;
    }
    state.opacity *= style_number(properties, "opacity", 1.0f);
    state.fill_opacity *= style_number(properties, "fill-opacity", 1.0f);
    state.stroke_opacity *= style_number(properties, "stroke-opacity", 1.0f);
    if (node.get_optional<std::string>("<xmlattr>.x")) {
        state.x_coordinates = svg_text_lengths(node, "x");
        state.x = state.x_coordinates.front();
    }
    if (node.get_optional<std::string>("<xmlattr>.y")) {
        state.y_coordinates = svg_text_lengths(node, "y");
        state.y = state.y_coordinates.front();
    }
    if (node.get_optional<std::string>("<xmlattr>.dx")) {
        state.dx_coordinates = svg_text_lengths(node, "dx");
        state.dx += state.dx_coordinates.front();
    }
    if (node.get_optional<std::string>("<xmlattr>.dy")) {
        state.dy_coordinates = svg_text_lengths(node, "dy");
        state.dy += state.dy_coordinates.front();
    }
}

} // namespace chronon3d::assets::svg_text_detail
