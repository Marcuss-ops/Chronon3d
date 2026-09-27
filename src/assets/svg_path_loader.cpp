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
};

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
    for (const auto* key : {"fill", "stroke", "stroke-width", "stroke-opacity", "fill-opacity", "opacity"}) {
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
        "polyline", "polygon"};
    static const std::set<std::string> supported_properties{
        "fill", "stroke", "stroke-width", "stroke-opacity", "fill-opacity",
        "opacity"};
    static const std::set<std::string> unsupported_paint_attributes{
        "color", "fill-rule", "clip-rule", "stroke-linecap", "stroke-linejoin",
        "stroke-miterlimit", "stroke-dasharray", "stroke-dashoffset", "paint-order",
        "clip-path", "mask", "filter", "visibility", "display", "mix-blend-mode",
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
    std::function<std::optional<std::string>(const boost::property_tree::ptree&)> visit;
    visit = [&](const boost::property_tree::ptree& current) -> std::optional<std::string> {
        for (const auto& [name, node] : current) {
            if (name == "<xmlattr>") continue;
            if (name == "<xmlcomment>") continue;
            if (name == "<xmltext>") {
                if (trim(node.data()).empty()) continue;
                return "SVG text nodes are not supported";
            }
            if (!supported_elements.contains(name))
                return "SVG element is not supported: <" + name + ">";

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
            for (const auto* key : {"fill", "stroke"}) {
                const auto it = properties.find(key);
                if (it == properties.end()) continue;
                auto value = trim(it->second);
                std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (value == "none") continue;
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

            if (auto error = visit(node)) return error;
        }
        return std::nullopt;
    };
    return visit(tree);
}

float style_number(const std::map<std::string, std::string>& style, const char* key, float fallback) {
    const auto it = style.find(key);
    if (it == style.end()) return fallback;
    try { return std::clamp(std::stof(it->second), 0.0f, 1.0f); } catch (...) { return fallback; }
}

void collect_path_data(
    const boost::property_tree::ptree& tree,
    std::vector<std::pair<std::string, Affine2>>& paths,
    std::vector<SvgStyleState>& styles,
    const Affine2& parent_transform = Affine2(1.0f),
    SvgStyleState parent_style = {}) {
    for (const auto& [name, node] : tree) {
        auto style = parent_style;
        const auto properties = style_properties(node);
        const Color current = style.fill ? style.fill->solid : Color::black();
        if (auto it = properties.find("fill"); it != properties.end()) {
            const auto color = parse_svg_color(it->second, current);
            style.fill = color ? std::optional<Fill>{Fill::solid_color(*color)} : std::nullopt;
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
        if (name == "path") {
            if (auto d = node.get_optional<std::string>("<xmlattr>.d")) {
                paths.emplace_back(*d, transform); styles.push_back(style);
            }
        } else if (name == "rect" || name == "circle" || name == "ellipse" ||
                   name == "line" || name == "polyline" || name == "polygon") {
            if (auto d = primitive_path_data(name, node); !d.empty()) {
                paths.emplace_back(std::move(d), transform); styles.push_back(style);
            }
        }
        collect_path_data(node, paths, styles, transform, style);
    }
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
            if (name == "text" || name == "image" || name == "use" || name == "style") {
                unsupported = name == "style" ? "CSS esterni" : name;
                return;
            }
            if (node.template get_optional<std::string>("<xmlattr>.href") ||
                node.template get_optional<std::string>("<xmlattr>.{http://www.w3.org/1999/xlink}href")) {
                unsupported = "CSS esterni";
                return;
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
    std::vector<std::pair<std::string, Affine2>> path_data;
    std::vector<SvgStyleState> styles;
    collect_path_data(document, path_data, styles);
    if (path_data.empty()) {
        return {.paths = {}, .ok = false, .error = "No <path d=\"...\"> found"};
    }

    SvgDocumentLoadResult result;
    result.paths.reserve(path_data.size());
    for (std::size_t index = 0; index < path_data.size(); ++index) {
        auto path = SvgImporter{}.import_path_data(path_data[index].first, options);
        if (!path.ok) {
            return {
                .paths = {},
                .ok = false,
                .error = "Failed to import <path> at index " +
                         std::to_string(index) + ": " + path.error,
            };
        }
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
        auto style = styles[index];
        style.stroke.color.a *= style.stroke_opacity;
        if (style.fill) style.fill->solid.a *= style.fill_opacity;
        path.path.fill = style.fill.value_or(Fill{.enabled = false});
        path.path.stroke = style.stroke;
        path.path.opacity = style.opacity;
        result.elements.push_back(SvgStyledElement{path.path, style.fill, style.stroke, style.opacity});
        result.paths.push_back(std::move(path.path));
    }
    result.ok = true;
    return result;
}

} // namespace chronon3d::assets
