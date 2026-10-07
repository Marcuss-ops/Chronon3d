#include <chronon3d/assets/svg_path_loader.hpp>

#include "svg_importer.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <functional>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <cctype>
#include <map>
#include <set>
#include <filesystem>
#include <limits>

namespace chronon3d::assets {
namespace {

using Affine2 = glm::mat3;

std::vector<float> transform_numbers(std::string_view text) {
    static const std::regex number(R"([-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?)");
    std::vector<float> values;
    for (std::cregex_iterator it(text.data(), text.data() + text.size(), number), end;
         it != end; ++it) {
        values.push_back(std::stof(it->str()));
    }
    return values;
}

Affine2 parse_transform(std::string_view text) {
    Affine2 result(1.0f);
    const auto translation = [](float x, float y) {
        return Affine2(
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(x, y, 1.0f));
    };
    const auto scaling = [](float x, float y) {
        return Affine2(
            glm::vec3(x, 0.0f, 0.0f),
            glm::vec3(0.0f, y, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f));
    };
    const auto rotation = [](float radians) {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return Affine2(
            glm::vec3(c, s, 0.0f),
            glm::vec3(-s, c, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f));
    };
    const auto skew_x = [](float radians) {
        return Affine2(
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(std::tan(radians), 1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f));
    };
    const auto skew_y = [](float radians) {
        return Affine2(
            glm::vec3(1.0f, std::tan(radians), 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f));
    };
    static const std::regex operation(R"(([A-Za-z]+)\s*\(([^)]*)\))");
    const std::string input(text);
    for (std::sregex_iterator it(input.begin(), input.end(), operation), end;
         it != end; ++it) {
        const auto name = (*it)[1].str();
        const auto values = transform_numbers((*it)[2].str());
        Affine2 local(1.0f);
        if (name == "matrix" && values.size() == 6) {
            local = Affine2(
                values[0], values[1], 0.0f,
                values[2], values[3], 0.0f,
                values[4], values[5], 1.0f);
        } else if (name == "translate" && !values.empty()) {
            local = translation(values[0], values.size() > 1 ? values[1] : 0.0f);
        } else if (name == "scale" && !values.empty()) {
            local = scaling(values[0], values.size() > 1 ? values[1] : values[0]);
        } else if (name == "rotate" && !values.empty()) {
            const float radians = glm::radians(values[0]);
            if (values.size() >= 3) {
                const glm::vec2 pivot(values[1], values[2]);
                local = translation(pivot.x, pivot.y) * rotation(radians) *
                    translation(-pivot.x, -pivot.y);
            } else {
                local = rotation(radians);
            }
        } else if (name == "skewX" && !values.empty()) {
            local = skew_x(glm::radians(values[0]));
        } else if (name == "skewY" && !values.empty()) {
            local = skew_y(glm::radians(values[0]));
        }
        result = result * local;
    }
    return result;
}

std::optional<Affine2> parse_viewbox_transform(
    const boost::property_tree::ptree& node) {
    const auto viewbox = node.get_optional<std::string>("<xmlattr>.viewBox");
    if (!viewbox) return std::nullopt;
    const auto values = transform_numbers(*viewbox);
    if (values.size() != 4 || values[2] <= 0.0f || values[3] <= 0.0f) {
        return std::nullopt;
    }

    const float viewport_width = node.get<float>("<xmlattr>.width", values[2]);
    const float viewport_height = node.get<float>("<xmlattr>.height", values[3]);
    if (viewport_width <= 0.0f || viewport_height <= 0.0f) return std::nullopt;

    // SVG's default preserveAspectRatio is xMidYMid meet.  Keep the
    // importer deterministic and geometry-only: explicit viewport dimensions
    // map the viewBox into that viewport while preserving its aspect ratio.
    const float scale = std::min(viewport_width / values[2], viewport_height / values[3]);
    const float offset_x = (viewport_width - values[2] * scale) * 0.5f;
    const float offset_y = (viewport_height - values[3] * scale) * 0.5f;
    return Affine2(
        glm::vec3(scale, 0.0f, 0.0f),
        glm::vec3(0.0f, scale, 0.0f),
        glm::vec3(offset_x - values[0] * scale, offset_y - values[1] * scale, 1.0f));
}

std::string primitive_path_data(
    const std::string& name,
    const boost::property_tree::ptree& node) {
    const auto attr = [&node](const char* key, float fallback = 0.0f) {
        return node.get<float>(std::string{"<xmlattr>."} + key, fallback);
    };
    std::ostringstream path;
    path << std::setprecision(9);
    if (name == "line") {
        path << "M " << attr("x1") << ' ' << attr("y1")
             << " L " << attr("x2") << ' ' << attr("y2");
    } else if (name == "rect") {
        const float x = attr("x");
        const float y = attr("y");
        const float width = attr("width");
        const float height = attr("height");
        if (width <= 0.0f || height <= 0.0f) return {};
        float rx = attr("rx");
        float ry = attr("ry");
        if (rx > 0.0f && ry == 0.0f) ry = rx;
        if (ry > 0.0f && rx == 0.0f) rx = ry;
        rx = std::min(rx, width * 0.5f);
        ry = std::min(ry, height * 0.5f);
        if (rx == 0.0f || ry == 0.0f) {
            path << "M " << x << ' ' << y
                 << " H " << x + width << " V " << y + height
                 << " H " << x << " Z";
        } else {
            path << "M " << x + rx << ' ' << y
                 << " H " << x + width - rx
                 << " A " << rx << ' ' << ry << " 0 0 1 " << x + width << ' ' << y + ry
                 << " V " << y + height - ry
                 << " A " << rx << ' ' << ry << " 0 0 1 " << x + width - rx << ' ' << y + height
                 << " H " << x + rx
                 << " A " << rx << ' ' << ry << " 0 0 1 " << x << ' ' << y + height - ry
                 << " V " << y + ry
                 << " A " << rx << ' ' << ry << " 0 0 1 " << x + rx << ' ' << y << " Z";
        }
    } else if (name == "circle" || name == "ellipse") {
        const float cx = attr("cx");
        const float cy = attr("cy");
        const float rx = attr(name == "circle" ? "r" : "rx");
        const float ry = attr(name == "circle" ? "r" : "ry");
        if (rx <= 0.0f || ry <= 0.0f) return {};
        path << "M " << cx - rx << ' ' << cy
             << " A " << rx << ' ' << ry << " 0 1 0 " << cx + rx << ' ' << cy
             << " A " << rx << ' ' << ry << " 0 1 0 " << cx - rx << ' ' << cy << " Z";
    } else if (name == "polyline" || name == "polygon") {
        const auto points = node.get_optional<std::string>("<xmlattr>.points");
        if (!points) return {};
        const auto values = transform_numbers(*points);
        if (values.size() < 4 || values.size() % 2 != 0) return {};
        path << "M " << values[0] << ' ' << values[1];
        for (std::size_t i = 2; i < values.size(); i += 2) {
            path << " L " << values[i] << ' ' << values[i + 1];
        }
        if (name == "polygon") path << " Z";
    }
    return path.str();
}

Vec2 transform_point(const Affine2& transform, Vec2 point) {
    const glm::vec3 transformed = transform * glm::vec3(point, 1.0f);
    return {transformed.x, transformed.y};
}

void apply_transform(PathShape& path, const Affine2& transform) {
    for (auto& command : path.commands) {
        if (command.type == PathCommandType::Close) continue;
        command.p0 = transform_point(transform, command.p0);
        if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo) {
            command.p1 = transform_point(transform, command.p1);
        }
        if (command.type == PathCommandType::CubicTo) {
            command.p2 = transform_point(transform, command.p2);
        }
    }
}

struct SvgStyleState {
    std::optional<Fill> fill{Fill::solid_color(Color::black())};
    PathStroke stroke{.enabled = false};
    float opacity{1.0f};
    float fill_opacity{1.0f};
    float stroke_opacity{1.0f};
    std::string fill_gradient_id;
    std::string vector_mask_id;
    bool vector_mask_is_mask{false};
    std::vector<SvgAnimationSpec> inherited_animations;
    std::optional<SvgMorphAnimationSpec> morph_animation;
};

struct SvgGradientSpec {
    bool radial{false};
    Vec2 p0{0.0f, 0.0f};
    Vec2 p1{1.0f, 0.0f};
    float radius{0.5f};
    Affine2 transform{1.0f};
    std::vector<graphics::GradientStop> stops;
    struct StopAnimation {
        std::size_t stop_index{0};
        bool opacity{false};
        Color from{1, 1, 1, 1};
        Color to{1, 1, 1, 1};
        float duration_seconds{0.0f};
        bool discrete{false};
    };
    std::vector<StopAnimation> animations;
};

using SvgGradientMap = std::map<std::string, SvgGradientSpec>;

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<Color> parse_svg_color(std::string value, Color current) {
    value = trim(std::move(value));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "none") return std::nullopt;
    if (value == "currentcolor") return current;
    static const std::map<std::string_view, std::string_view> named_colors{
        {"black", "#000000"}, {"silver", "#C0C0C0"}, {"gray", "#808080"},
        {"grey", "#808080"}, {"white", "#FFFFFF"}, {"maroon", "#800000"},
        {"red", "#FF0000"}, {"purple", "#800080"}, {"fuchsia", "#FF00FF"},
        {"green", "#008000"}, {"lime", "#00FF00"}, {"olive", "#808000"},
        {"yellow", "#FFFF00"}, {"navy", "#000080"}, {"blue", "#0000FF"},
        {"teal", "#008080"}, {"aqua", "#00FFFF"}, {"orange", "#FFA500"}
    };
    if (const auto named = named_colors.find(value); named != named_colors.end()) {
        try { return Color::from_hex(std::string{named->second}.c_str()); }
        catch (...) { return std::nullopt; }
    }
    if (value.size() == 4 && value[0] == '#') {
        const auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
        const int r = digit(value[1]), g = digit(value[2]), b = digit(value[3]);
        if (r < 0 || g < 0 || b < 0) return std::nullopt;
        return Color{r / 15.0f, g / 15.0f, b / 15.0f, 1.0f};
    }
    if (value.size() == 7 && value[0] == '#') {
        try { return Color::from_hex(value.c_str()); } catch (...) { return std::nullopt; }
    }
    if (value.rfind("rgb(", 0) == 0 && value.back() == ')') {
        const auto parts = transform_numbers(std::string_view(value).substr(4, value.size() - 5));
        if (parts.size() == 3) return Color{std::clamp(parts[0], 0.0f, 255.0f) / 255.0f, std::clamp(parts[1], 0.0f, 255.0f) / 255.0f, std::clamp(parts[2], 0.0f, 255.0f) / 255.0f, 1.0f};
    }
    return std::nullopt;
}

std::map<std::string, std::string> style_properties(const boost::property_tree::ptree& node) {
    std::map<std::string, std::string> values;
    for (const auto* key : {"fill", "stroke", "stroke-width", "stroke-opacity", "fill-opacity", "opacity", "stop-color", "stop-opacity", "clip-path", "mask"}) {
        if (auto value = node.get_optional<std::string>(std::string("<xmlattr>.") + key)) values[key] = *value;
    }
    if (auto style = node.get_optional<std::string>("<xmlattr>.style")) {
        std::istringstream input(*style);
        std::string item;
        while (std::getline(input, item, ';')) {
            const auto colon = item.find(':');
            if (colon == std::string::npos) continue;
            auto key = trim(item.substr(0, colon));
            auto value = trim(item.substr(colon + 1));
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            values[key] = value;
        }
    }
    return values;
}

std::optional<std::string> validate_svg_subset(
    const boost::property_tree::ptree& tree) {
    static const std::set<std::string> supported_elements{
        "svg", "g", "path", "rect", "circle", "ellipse", "line",
        "polyline", "polygon", "animate", "animateTransform", "defs",
        "linearGradient", "radialGradient", "stop", "use", "clipPath", "mask"};
    static const std::set<std::string> supported_properties{
        "fill", "stroke", "stroke-width", "stroke-opacity", "fill-opacity",
        "opacity", "stop-color", "stop-opacity", "clip-path", "mask"};
    static const std::set<std::string> unsupported_paint_attributes{
        "color", "fill-rule", "clip-rule", "stroke-linecap", "stroke-linejoin",
        "stroke-miterlimit", "stroke-dasharray", "stroke-dashoffset", "paint-order",
        "filter", "visibility", "display", "mix-blend-mode",
        "vector-effect"};
    const auto valid_unit_interval = [](const std::string& text) {
        try {
            std::size_t consumed = 0;
            const float value = std::stof(text, &consumed);
            return consumed == text.size() && std::isfinite(value) &&
                   value >= 0.0f && value <= 1.0f;
        } catch (...) {
            return false;
        }
    };
    const auto valid_stroke_width = [](const std::string& text) {
        try {
            std::size_t consumed = 0;
            const float value = std::stof(text, &consumed);
            return consumed == text.size() && std::isfinite(value) &&
                   value >= 0.0f && value <= 100000.0f;
        } catch (...) {
            return false;
        }
    };
    std::function<std::optional<std::string>(const boost::property_tree::ptree&, const std::string&)> visit;
    visit = [&](const boost::property_tree::ptree& current,
                const std::string& parent_name) -> std::optional<std::string> {
        for (const auto& [name, node] : current) {
            if (name == "<xmlattr>") continue;
            if (name == "<xmlcomment>") continue;
            if (name == "<xmltext>") {
                if (trim(node.data()).empty()) continue;
                return "SVG text nodes are not supported";
            }
            if (!supported_elements.contains(name))
                return "SVG element is not supported: <" + name + ">";
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
                        } catch (...) { return "SVG <use> coordinates must be finite and bounded"; }
                    }
                }
            }

            if (const auto style = node.get_optional<std::string>("<xmlattr>.style")) {
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
            for (const auto* key : {"clip-path", "mask"}) {
                if (const auto it = properties.find(key); it != properties.end()) {
                    const auto value = trim(it->second);
                    if ((name != "svg" && name != "g" && name != "path") ||
                        !value.starts_with("url(#") || !value.ends_with(')') || value.size() <= 6)
                        return std::string("SVG ") + key + " supports only root, group, or path references to url(#id)";
                }
            }
            if (name == "clipPath" || name == "mask") {
                if (parent_name != "defs") return "SVG clipPath and mask definitions must be children of <defs>";
            }
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
                if (value == "none" || (key == "fill" && value.starts_with("url(#") && value.ends_with(")"))) continue;
                if (name == "stop" && key == "fill") continue;
                if (!parse_svg_color(it->second, Color::black()))
                    return std::string("SVG ") + key + " color is invalid or unsupported";
            }
            for (const auto* key : {"opacity", "fill-opacity", "stroke-opacity"}) {
                const auto it = properties.find(key);
                if (it != properties.end() && !valid_unit_interval(trim(it->second)))
                    return std::string("SVG ") + key + " must be a finite number in [0,1]";
            }
            if (const auto it = properties.find("stroke-width");
                it != properties.end() && !valid_stroke_width(trim(it->second)))
                return "SVG stroke-width must be finite and within [0,100000]";

            if (name == "linearGradient" || name == "radialGradient" || name == "stop") continue;
            if (auto error = visit(node, name)) return error;
        }
        return std::nullopt;
    };
    return visit(tree, {});
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
    } catch (...) { return false; }
}

std::optional<std::string> collect_svg_gradients(
    const boost::property_tree::ptree& tree, SvgGradientMap& gradients) {
    for (const auto& [name, node] : tree) {
        if (name == "linearGradient" || name == "radialGradient") {
            const auto id = node.get_optional<std::string>("<xmlattr>.id");
            if (!id || id->empty() || id->size() > 128 || gradients.contains(*id))
                return "SVG gradient requires a unique bounded id";
            if (node.get<std::string>("<xmlattr>.gradientUnits", "objectBoundingBox") != "objectBoundingBox" ||
                node.get<std::string>("<xmlattr>.spreadMethod", "pad") != "pad" ||
                node.get_optional<std::string>("<xmlattr>.href") ||
                node.get_optional<std::string>("<xmlattr>.{http://www.w3.org/1999/xlink}href"))
                return "SVG gradient supports objectBoundingBox units, pad spread, and no inheritance";
            SvgGradientSpec gradient;
            gradient.radial = name == "radialGradient";
            if (const auto transform = node.get_optional<std::string>("<xmlattr>.gradientTransform"))
                gradient.transform = parse_transform(*transform);
            auto coordinate = [&](const char* key, const char* fallback, float& out) {
                return parse_svg_scalar(node.get<std::string>(std::string("<xmlattr>.") + key, fallback), 0.0f, out);
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
                const auto color_it = props.find("stop-color");
                const auto color = parse_svg_color(color_it == props.end() ? "black" : color_it->second, Color::black());
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
                    } catch (...) { return "SVG gradient animation duration is invalid"; }
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
                        const auto from_color = parse_svg_color(from_text, Color::black());
                        const auto to_color = parse_svg_color(to_text, Color::black());
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
            if (auto error = collect_svg_gradients(node, gradients)) return error;
        }
    }
    return std::nullopt;
}

bool parse_svg_gradient_reference(const std::string& text, std::string& id) {
    const auto value = trim(text);
    if (!value.starts_with("url(#") || !value.ends_with(')') || value.size() <= 6) return false;
    id = value.substr(5, value.size() - 6);
    return !id.empty() && id.find_first_of(" \t\r\n") == std::string::npos;
}

float style_number(const std::map<std::string, std::string>& style, const char* key, float fallback) {
    const auto it = style.find(key);
    if (it == style.end()) return fallback;
    try { return std::clamp(std::stof(it->second), 0.0f, 1.0f); } catch (...) { return fallback; }
}

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
        } catch (...) { return false; }
    };
    const auto parse_values = [](const std::string& text, std::vector<float>& values) {
        if (text.empty()) return false;
        for (const char c : text) {
            if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' ||
                  c == 'e' || c == 'E' || c == ',' || std::isspace(static_cast<unsigned char>(c))))
                return false;
        }
        values = transform_numbers(text);
        return !values.empty() && values.size() <= 2 &&
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
        if (expected == 0 || from.size() != expected || to.size() != expected) return false;
        if (type == "scale" && (std::abs(from[0] - from[1]) > 1e-6f ||
                                 std::abs(to[0] - to[1]) > 1e-6f)) return false;
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
    else if (transform) output.components = static_cast<std::uint8_t>(from.size());
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
        } catch (...) { return false; }
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

void collect_path_data(
    const boost::property_tree::ptree& tree,
    std::vector<std::pair<std::string, Affine2>>& paths,
    std::vector<SvgStyleState>& styles,
    std::vector<std::vector<SvgAnimationSpec>>& animations,
    const SvgGradientMap& gradients,
    const std::map<std::string, std::pair<std::string, const boost::property_tree::ptree*>>& ids,
    std::size_t use_depth,
    std::size_t& expanded_uses,
    const Affine2& parent_transform = Affine2(1.0f),
    SvgStyleState parent_style = {}) {
    for (const auto& [name, node] : tree) {
        if (name == "animate" || name == "animateTransform" || name == "defs" ||
            name == "linearGradient" || name == "radialGradient" || name == "stop") continue;
        auto style = parent_style;
        const auto properties = style_properties(node);
        const Color current = style.fill ? style.fill->solid : Color::black();
        if (auto it = properties.find("fill"); it != properties.end()) {
            std::string gradient_id;
            if (parse_svg_gradient_reference(it->second, gradient_id)) {
                style.fill_gradient_id = gradient_id;
                if (!gradients.contains(gradient_id)) style.fill.reset();
            } else {
                style.fill_gradient_id.clear();
                const auto color = parse_svg_color(it->second, current);
                style.fill = color ? std::optional<Fill>{Fill::solid_color(*color)} : std::nullopt;
            }
        }
        for (const auto* key : {"clip-path", "mask"}) {
            if (const auto it = properties.find(key); it != properties.end()) {
                std::string reference_id;
                if (parse_svg_gradient_reference(it->second, reference_id)) {
                    style.vector_mask_id = std::move(reference_id);
                    style.vector_mask_is_mask = std::string_view(key) == "mask";
                } else {
                    style.vector_mask_id.clear();
                }
            }
        }
        if (auto it = properties.find("stroke"); it != properties.end()) {
            const auto color = parse_svg_color(it->second, current);
            style.stroke.enabled = color.has_value();
            if (color) style.stroke.color = *color;
        }
        if (auto it = properties.find("stroke-width"); it != properties.end()) {
            try { style.stroke.width = std::max(0.0f, std::stof(it->second)); } catch (...) {}
        }
        style.opacity *= style_number(properties, "opacity", 1.0f);
        style.fill_opacity *= style_number(properties, "fill-opacity", 1.0f);
        style.stroke_opacity *= style_number(properties, "stroke-opacity", 1.0f);
        const auto local_transform = node.get_optional<std::string>("<xmlattr>.transform");
        Affine2 node_transform = local_transform ? parse_transform(*local_transform) : Affine2(1.0f);
        if (name == "svg") {
            if (auto viewbox_transform = parse_viewbox_transform(node)) {
                node_transform = node_transform * *viewbox_transform;
            }
        }
        const Affine2 transform = parent_transform * node_transform;
        if (name == "g") {
            for (const auto& child : node) {
                if (child.first != "animate" && child.first != "animateTransform") continue;
                SvgAnimationSpec animation;
                if (style.inherited_animations.size() >= 9 ||
                    !parse_svg_animation(child.first, child.second, animation)) {
                    style.inherited_animations.clear();
                    style.inherited_animations.push_back(SvgAnimationSpec{});
                    style.inherited_animations.back().duration_seconds = -1.0f;
                    break;
                }
                style.inherited_animations.push_back(animation);
            }
        }
        if (name == "use") {
            const auto href = node.get<std::string>("<xmlattr>.href", "");
            const auto target = ids.find(href.size() > 1 ? href.substr(1) : std::string{});
            if (target == ids.end() || use_depth >= 16 || expanded_uses >= 4096 ||
                target->second.first == "use" || target->second.first == "defs" ||
                target->second.first == "linearGradient" || target->second.first == "radialGradient" ||
                target->second.first == "stop") {
                paths.emplace_back(std::string{}, transform);
                styles.push_back(style);
                animations.emplace_back(1, SvgAnimationSpec{});
                animations.back().front().duration_seconds = -1.0f;
                continue;
            }
            ++expanded_uses;
            const float x = node.get<float>("<xmlattr>.x", 0.0f);
            const float y = node.get<float>("<xmlattr>.y", 0.0f);
            std::ostringstream translation;
            translation << "translate(" << x << ' ' << y << ')';
            const Affine2 use_transform = transform * parse_transform(translation.str());
            boost::property_tree::ptree wrapper;
            wrapper.add_child(target->second.first, *target->second.second);
            collect_path_data(wrapper, paths, styles, animations, gradients, ids,
                              use_depth + 1, expanded_uses, use_transform, style);
            continue;
        }
        if (name == "path") {
            if (auto d = node.get_optional<std::string>("<xmlattr>.d")) {
                paths.emplace_back(*d, transform); styles.push_back(style);
                animations.emplace_back(style.inherited_animations);
                for (const auto& child : node) {
                    if (child.first != "animate" && child.first != "animateTransform") continue;
                    if (child.first == "animate" &&
                        child.second.get<std::string>("<xmlattr>.attributeName", "") == "d") {
                        SvgMorphAnimationSpec morph;
                        if (style.morph_animation || !parse_svg_morph_animation(child.second, morph)) {
                            animations.back().push_back(SvgAnimationSpec{});
                            animations.back().back().duration_seconds = -1.0f;
                            break;
                        }
                        style.morph_animation = std::move(morph);
                        continue;
                    }
                    SvgAnimationSpec animation;
                    if (animations.back().size() >= 9 || !parse_svg_animation(child.first, child.second, animation)) {
                        animations.back().clear();
                        animations.back().push_back(SvgAnimationSpec{});
                        animations.back().back().duration_seconds = -1.0f;
                        break;
                    }
                    animations.back().push_back(animation);
                }
                const Affine2 identity(1.0f);
                const bool has_static_transform = [&] {
                    for (int column = 0; column < 3; ++column)
                        for (int row = 0; row < 3; ++row)
                            if (std::abs(transform[column][row] - identity[column][row]) > 1e-6f)
                                return true;
                    return false;
                }();
                if (has_static_transform && !animations.back().empty()) {
                    animations.back().clear();
                    animations.back().push_back(SvgAnimationSpec{});
                    animations.back().back().duration_seconds = -1.0f;
                }
                styles.back().morph_animation = std::move(style.morph_animation);
            }
        } else if (name == "rect" || name == "circle" || name == "ellipse" ||
                   name == "line" || name == "polyline" || name == "polygon") {
            if (auto d = primitive_path_data(name, node); !d.empty()) {
                paths.emplace_back(std::move(d), transform); styles.push_back(style);
                animations.emplace_back(style.inherited_animations);
            }
        }
        collect_path_data(node, paths, styles, animations, gradients, ids,
                          use_depth, expanded_uses, transform, style);
    }
}

std::optional<std::string> collect_vector_mask_geometry(
    const std::string& definition_kind,
    const boost::property_tree::ptree& definition,
    const Affine2& root_transform,
    PathShape& output) {
    if (definition_kind == "clipPath" &&
        definition.get<std::string>("<xmlattr>.clipPathUnits", "userSpaceOnUse") != "userSpaceOnUse")
        return "SVG clipPath supports only userSpaceOnUse coordinates";
    if (definition_kind == "mask" &&
        (definition.get<std::string>("<xmlattr>.maskUnits", "objectBoundingBox") != "userSpaceOnUse" ||
         definition.get<std::string>("<xmlattr>.maskContentUnits", "userSpaceOnUse") != "userSpaceOnUse"))
        return "SVG mask supports only explicit userSpaceOnUse units";
    const auto definition_transform = definition.get_optional<std::string>("<xmlattr>.transform");
    const Affine2 base = root_transform * (definition_transform ? parse_transform(*definition_transform) : Affine2(1.0f));
    std::size_t geometry_count = 0;
    std::function<std::optional<std::string>(const boost::property_tree::ptree&, const Affine2&)> visit;
    visit = [&](const boost::property_tree::ptree& tree, const Affine2& parent) -> std::optional<std::string> {
        for (const auto& [name, node] : tree) {
            if (name == "<xmlattr>" || name == "<xmltext>" || name == "<xmlcomment>") continue;
            const auto transform_text = node.get_optional<std::string>("<xmlattr>.transform");
            const Affine2 transform = parent * (transform_text ? parse_transform(*transform_text) : Affine2(1.0f));
            std::string d;
            if (name == "path") d = node.get<std::string>("<xmlattr>.d", "");
            else if (name == "rect" || name == "circle" || name == "ellipse" || name == "line" ||
                     name == "polyline" || name == "polygon") d = primitive_path_data(name, node);
            else if (name == "g") {
                if (auto error = visit(node, transform)) return error;
                continue;
            } else {
                return "SVG vector masks support only path and primitive geometry";
            }
            if (++geometry_count > 1)
                return "SVG vector clip/mask currently requires exactly one geometry element";
            if (definition_kind == "mask") {
                const auto paint = style_properties(node);
                const auto fill = paint.find("fill");
                const auto opacity = paint.find("opacity");
                if (fill == paint.end() || trim(fill->second) != "white" ||
                    (opacity != paint.end() && trim(opacity->second) != "1"))
                    return "SVG mask geometry must be explicitly opaque white";
            }
            if (d.empty()) return "SVG vector mask geometry is empty";
            auto imported = SvgImporter{}.import_path_data(d, {});
            if (!imported.ok) return "SVG vector mask path is malformed: " + imported.error;
            apply_transform(imported.path, transform);
            output.commands.insert(output.commands.end(), imported.path.commands.begin(), imported.path.commands.end());
            if (output.commands.size() > 65536) return "SVG vector mask exceeds 65536 path commands";
        }
        return std::nullopt;
    };
    if (auto error = visit(definition, base)) return error;
    if (geometry_count != 1) return "SVG vector clip/mask must contain exactly one geometry element";
    output.fill = Fill::solid_color(Color::white());
    output.stroke.enabled = false;
    return std::nullopt;
}

} // namespace

SvgPathLoadResult parse_svg_path_data(std::string_view d, SvgPathLoadOptions options) {
    return SvgImporter{}.import_path_data(d, options);
}

SvgDocumentLoadResult load_svg_document_file(
    const std::string& filename,
    SvgPathLoadOptions options) {
    constexpr std::uintmax_t kMaxSvgBytes = 8U * 1024U * 1024U;
    constexpr std::size_t kMaxXmlNodes = 100000;
    constexpr std::size_t kMaxCommandsPerPath = 65536;
    std::error_code size_error;
    const auto byte_size = std::filesystem::file_size(filename, size_error);
    if (size_error || byte_size > kMaxSvgBytes)
        return {.paths = {}, .elements = {}, .ok = false,
                .error = "SVG document exceeds the 8 MiB resource limit"};
    boost::property_tree::ptree document;
    try {
        boost::property_tree::read_xml(
            filename, document,
            boost::property_tree::xml_parser::trim_whitespace);
    } catch (const boost::property_tree::xml_parser::xml_parser_error& e) {
        return {.paths = {}, .ok = false, .error = e.what()};
    }

    std::string unsupported;
    std::size_t xml_nodes = 0;
    std::function<void(const boost::property_tree::ptree&)> inspect = [&](const auto& tree) {
        for (const auto& [name, node] : tree) {
            if (name == "<xmlattr>") continue;
            if (name == "<xmlcomment>") continue;
            if (name == "<xmltext>") {
                if (!trim(node.data()).empty()) unsupported = "text";
                if (!unsupported.empty()) return;
                continue;
            }
            if (++xml_nodes > kMaxXmlNodes) {
                unsupported = "resource limit: XML node count exceeds 100000";
                return;
            }
            if (name == "text" || name == "image" || name == "style") {
                unsupported = name == "style" ? "CSS esterni" : name;
                return;
            }
            if (node.template get_optional<std::string>("<xmlattr>.href") ||
                node.template get_optional<std::string>("<xmlattr>.{http://www.w3.org/1999/xlink}href")) {
                const auto href = node.template get_optional<std::string>("<xmlattr>.href");
                if (name != "use" || !href || href->size() < 2 || href->front() != '#') {
                    unsupported = "CSS esterni";
                    return;
                }
            }
            inspect(node);
            if (!unsupported.empty()) return;
        }
    };
    inspect(document);
    if (!unsupported.empty()) {
        if (unsupported.starts_with("resource limit:"))
            return {.paths = {}, .elements = {}, .ok = false, .error = "SVG " + unsupported};
        const auto tag = unsupported == "CSS esterni" ? "style" : unsupported.c_str();
        return {.paths = {}, .elements = {}, .ok = false,
                .error = std::string("SVG: ") + unsupported + " non supportata: <" + tag + ">"};
    }
    if (const auto style_error = validate_svg_subset(document))
        return {.paths = {}, .elements = {}, .ok = false,
                .error = *style_error};
    SvgGradientMap gradients;
    if (const auto gradient_error = collect_svg_gradients(document, gradients))
        return {.paths = {}, .elements = {}, .ok = false, .error = *gradient_error};
    std::map<std::string, std::pair<std::string, const boost::property_tree::ptree*>> ids;
    std::optional<std::string> duplicate_id_error;
    std::function<void(const boost::property_tree::ptree&)> collect_ids = [&](const auto& tree) {
        for (const auto& [name, node] : tree) {
            if (name == "<xmlattr>" || name == "<xmlcomment>" || name == "<xmltext>") continue;
            if (const auto id = node.template get_optional<std::string>("<xmlattr>.id")) {
                if (id->empty() || id->size() > 128 || !ids.emplace(*id, std::pair{name, &node}).second)
                    duplicate_id_error = "SVG ids must be unique, non-empty, and at most 128 bytes";
            }
            collect_ids(node);
        }
    };
    collect_ids(document);
    if (duplicate_id_error)
        return {.paths = {}, .elements = {}, .ok = false, .error = *duplicate_id_error};
    std::optional<PathShape> vector_mask;
    const boost::property_tree::ptree* svg_root = nullptr;
    for (const auto& [name, node] : document)
        if (name == "svg") { svg_root = &node; break; }
    if (!svg_root)
        return {.paths = {}, .elements = {}, .ok = false, .error = "SVG document root <svg> is missing"};
    const auto root_properties = style_properties(*svg_root);
    const auto clip_ref = root_properties.find("clip-path");
    const auto mask_ref = root_properties.find("mask");
    if (clip_ref != root_properties.end() && mask_ref != root_properties.end())
        return {.paths = {}, .elements = {}, .ok = false,
                .error = "SVG root cannot combine clip-path and mask in the bounded vector-mask subset"};
    const auto vector_ref = clip_ref != root_properties.end() ? clip_ref : mask_ref;
    if (vector_ref != root_properties.end()) {
        std::string reference_id;
        if (!parse_svg_gradient_reference(vector_ref->second, reference_id))
            return {.paths = {}, .elements = {}, .ok = false, .error = "SVG vector mask reference is malformed"};
        const auto target = ids.find(reference_id);
        const std::string expected_kind = clip_ref != root_properties.end() ? "clipPath" : "mask";
        if (target == ids.end() || target->second.first != expected_kind)
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG vector mask reference does not resolve to a matching definition"};
        auto root_transform_text = svg_root->get_optional<std::string>("<xmlattr>.transform");
        Affine2 root_transform = root_transform_text ? parse_transform(*root_transform_text) : Affine2(1.0f);
        if (auto viewbox = parse_viewbox_transform(*svg_root)) root_transform = root_transform * *viewbox;
        PathShape mask_geometry;
        if (const auto mask_error = collect_vector_mask_geometry(expected_kind,
                *target->second.second, root_transform, mask_geometry))
            return {.paths = {}, .elements = {}, .ok = false, .error = *mask_error};
        vector_mask = std::move(mask_geometry);
    }
    // Keep validation before collection: collect_path_data uses forgiving
    // conversions, while this pass rejects malformed style values explicitly.
    std::vector<std::pair<std::string, Affine2>> path_data;
    std::vector<SvgStyleState> styles;
    std::vector<std::vector<SvgAnimationSpec>> animations;
    std::size_t expanded_uses = 0;
    collect_path_data(document, path_data, styles, animations, gradients, ids, 0, expanded_uses);
    const bool root_mask_reference = vector_ref != root_properties.end();
    if (root_mask_reference) {
        std::string root_mask_id;
        (void)parse_svg_gradient_reference(vector_ref->second, root_mask_id);
        for (const auto& style : styles)
            if (style.vector_mask_id != root_mask_id)
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG root clip/mask cannot be overridden by individual paths"};
    } else {
        const auto local_mask = std::find_if(styles.begin(), styles.end(), [](const auto& style) {
            return !style.vector_mask_id.empty();
        });
        if (local_mask != styles.end()) {
            if (path_data.size() != 1 || styles[0].vector_mask_id.empty())
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG group/path clip and mask references currently require exactly one emitted path"};
            const auto transform_animation = [](const auto& animation) {
                return animation.kind == SvgAnimationKind::Translate ||
                    animation.kind == SvgAnimationKind::Scale || animation.kind == SvgAnimationKind::Rotate;
            };
            if (std::any_of(animations[0].begin(), animations[0].end(), transform_animation))
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG local clip/mask cannot be combined with animated path/group transforms"};
            for (const auto& style : styles)
                if (style.vector_mask_id != styles[0].vector_mask_id ||
                    style.vector_mask_is_mask != styles[0].vector_mask_is_mask)
                    return {.paths = {}, .elements = {}, .ok = false,
                            .error = "SVG clip/mask scopes must agree for the single emitted path"};
            const auto target = ids.find(styles[0].vector_mask_id);
            const std::string expected = styles[0].vector_mask_is_mask ? "mask" : "clipPath";
            if (target == ids.end() || target->second.first != expected)
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG path clip/mask reference does not resolve to a matching definition"};
            PathShape mask_geometry;
            if (const auto error = collect_vector_mask_geometry(expected, *target->second.second,
                                                                  path_data[0].second, mask_geometry))
                return {.paths = {}, .elements = {}, .ok = false, .error = *error};
            vector_mask = std::move(mask_geometry);
        }
    }
    if (std::any_of(path_data.begin(), path_data.end(), [](const auto& path) { return path.first.empty(); }))
        return {.paths = {}, .elements = {}, .ok = false,
                .error = "SVG <use> reference is unresolved, recursive, out of bounds, or does not target geometry"};
    if (path_data.empty()) {
        return {.paths = {}, .ok = false, .error = "No <path d=\"...\"> found"};
    }

    SvgDocumentLoadResult result;
    result.paths.reserve(path_data.size());
    result.vector_mask = vector_mask;
    for (std::size_t index = 0; index < path_data.size(); ++index) {
        if (std::any_of(animations[index].begin(), animations[index].end(), [](const auto& animation) {
                return animation.duration_seconds < 0.0f;
            }))
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG animation is malformed or outside the supported bounded subset"};
        auto path = SvgImporter{}.import_path_data(path_data[index].first, options);
        if (!path.ok) {
            return {
                .paths = {},
                .ok = false,
                .error = "Failed to import <path> at index " +
                         std::to_string(index) + ": " + path.error,
            };
        }
        auto style = styles[index];
        std::vector<SvgGradientAnimationKeyframe> gradient_animation;
        apply_transform(path.path, path_data[index].second);
        if (path.path.commands.size() > kMaxCommandsPerPath)
            return {.paths = {}, .elements = {}, .ok = false,
                    .error = "SVG resource limit: path command count exceeds 65536"};
        const auto quantize = [](Vec2& point) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                std::abs(point.x) > 100000.0f || std::abs(point.y) > 100000.0f)
                return false;
            point.x = std::round(point.x * 10000.0f) / 10000.0f;
            point.y = std::round(point.y * 10000.0f) / 10000.0f;
            return true;
        };
        for (auto& command : path.path.commands) {
            if (!quantize(command.p0) || !quantize(command.p1) || !quantize(command.p2))
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG coordinate exceeds finite resource bounds"};
        }
        if (style.morph_animation) {
            auto& morph = *style.morph_animation;
            if (morph.paths.size() < 2 || morph.paths.size() != morph.key_times.size())
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG path morph requires matching values and keyTimes"};
            const auto& topology = morph.paths.front();
            if (topology.commands.empty() || topology.commands.size() > kMaxCommandsPerPath)
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG morph path is empty or exceeds 65536 commands"};
            for (auto& target : morph.paths) {
                apply_transform(target, path_data[index].second);
                if (target.commands.size() != topology.commands.size() || target.closed != topology.closed)
                    return {.paths = {}, .elements = {}, .ok = false,
                            .error = "SVG path morph requires matching command topology"};
                for (std::size_t command_index = 0; command_index < target.commands.size(); ++command_index) {
                    auto& command = target.commands[command_index];
                    if (command.type != topology.commands[command_index].type ||
                        !quantize(command.p0) || !quantize(command.p1) || !quantize(command.p2))
                        return {.paths = {}, .elements = {}, .ok = false,
                                .error = "SVG path morph requires matching command topology and bounded coordinates"};
                }
            }
            path.path = morph.paths.front();
        }
        if (!style.fill_gradient_id.empty()) {
            const auto gradient_it = gradients.find(style.fill_gradient_id);
            if (gradient_it == gradients.end())
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG fill references an unknown gradient id"};
            float min_x = std::numeric_limits<float>::infinity();
            float min_y = std::numeric_limits<float>::infinity();
            float max_x = -min_x, max_y = -min_y;
            for (const auto& command : path.path.commands) {
                if (command.type == PathCommandType::Close) continue;
                std::array<Vec2, 3> points{command.p0, command.p1, command.p2};
                const std::size_t point_count = command.type == PathCommandType::CubicTo ? 3 :
                    command.type == PathCommandType::QuadraticTo ? 2 : 1;
                for (std::size_t point_index = 0; point_index < point_count; ++point_index) {
                    const auto point = points[point_index];
                    min_x = std::min(min_x, point.x); min_y = std::min(min_y, point.y);
                    max_x = std::max(max_x, point.x); max_y = std::max(max_y, point.y);
                }
            }
            const float width = max_x - min_x, height = max_y - min_y;
            if (!(width > 0.0f) || !(height > 0.0f))
                return {.paths = {}, .elements = {}, .ok = false,
                        .error = "SVG objectBoundingBox gradient requires non-degenerate path bounds"};
            const auto& source = gradient_it->second;
            const Vec2 origin = transform_point(source.transform, {0.0f, 0.0f});
            const Vec2 axis_x = transform_point(source.transform, {1.0f, 0.0f}) - origin;
            const Vec2 axis_y = transform_point(source.transform, {0.0f, 1.0f}) - origin;
            const auto bbox_point = [&](Vec2 normalized) {
                const Vec2 transformed = transform_point(source.transform, normalized);
                return Vec2{min_x + transformed.x * width, min_y + transformed.y * height};
            };
            auto stops = source.stops;
            for (auto& stop : stops) stop.color.a *= style.fill_opacity;
            if (source.radial) {
                if (std::abs(width - height) > 1e-4f * std::max(width, height))
                    return {.paths = {}, .elements = {}, .ok = false,
                            .error = "SVG radial objectBoundingBox gradient requires a square path bound"};
                const float sx = glm::length(axis_x), sy = glm::length(axis_y);
                if (!std::isfinite(sx) || !std::isfinite(sy) || sx <= 0.0f ||
                    std::abs(sx - sy) > 1e-5f * std::max(sx, sy) ||
                    std::abs(glm::dot(axis_x, axis_y)) > 1e-5f * sx * sy)
                    return {.paths = {}, .elements = {}, .ok = false,
                            .error = "SVG radial gradientTransform must preserve circular geometry"};
                const auto center = bbox_point(source.p0);
                const float radius = source.radius * sx * width;
                if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(radius) || radius <= 0.0f)
                    return {.paths = {}, .elements = {}, .ok = false,
                            .error = "SVG radial gradientTransform exceeds finite bounds"};
                style.fill = Fill::radial(center, radius, std::move(stops));
            } else {
                const auto start = bbox_point(source.p0);
                const auto end = bbox_point(source.p1);
                if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
                    !std::isfinite(end.x) || !std::isfinite(end.y) ||
                    std::abs(start.x) > 100000.0f || std::abs(start.y) > 100000.0f ||
                    std::abs(end.x) > 100000.0f || std::abs(end.y) > 100000.0f)
                    return {.paths = {}, .elements = {}, .ok = false,
                            .error = "SVG linear gradientTransform exceeds finite bounds"};
                style.fill = Fill::linear(start, end, std::move(stops));
            }
            if (!source.animations.empty() && style.fill && style.fill->enabled) {
                const std::vector<float> event_times{0.0f, source.animations.front().duration_seconds};
                for (const float seconds : event_times) {
                    auto gradient = style.fill->gradient;
                    for (const auto& animation : source.animations) {
                        if (animation.stop_index >= gradient.color_stops.size())
                            return {.paths = {}, .elements = {}, .ok = false,
                                    .error = "SVG animated gradient stop index is invalid"};
                        const float progress = animation.discrete
                            ? (seconds >= animation.duration_seconds ? 1.0f : 0.0f)
                            : std::clamp(seconds / animation.duration_seconds, 0.0f, 1.0f);
                        const auto mix = [progress](float from, float to) {
                            return from + (to - from) * progress;
                        };
                        auto& stop = gradient.color_stops[animation.stop_index];
                        if (animation.opacity) {
                            stop.color.a = mix(animation.from.a, animation.to.a) * style.fill_opacity;
                        } else {
                            stop.color.r = mix(animation.from.r, animation.to.r);
                            stop.color.g = mix(animation.from.g, animation.to.g);
                            stop.color.b = mix(animation.from.b, animation.to.b);
                        }
                    }
                    gradient_animation.push_back({seconds, std::move(gradient)});
                }
                style.fill->gradient = gradient_animation.front().gradient;
            }
        }
        style.stroke.color.a *= style.stroke_opacity;
        if (style.fill && is_solid_fill(*style.fill)) style.fill->solid.a *= style.fill_opacity;
        path.path.fill = style.fill.value_or(Fill{.enabled = false});
        path.path.stroke = style.stroke;
        path.path.opacity = style.opacity;
        result.elements.push_back(SvgStyledElement{
            path.path, style.fill, style.stroke, style.opacity, std::move(animations[index]),
            std::move(style.morph_animation), std::move(gradient_animation),
            !style.fill_gradient_id.empty() &&
                !gradients.at(style.fill_gradient_id).animations.empty() &&
                gradients.at(style.fill_gradient_id).animations.front().discrete});
        result.paths.push_back(std::move(path.path));
    }
    result.ok = true;
    return result;
}

} // namespace chronon3d::assets
