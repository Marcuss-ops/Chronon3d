#include <chronon3d/assets/svg_path_loader.hpp>

#include "svg_importer.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

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

void collect_path_data(
    const boost::property_tree::ptree& tree,
    std::vector<std::pair<std::string, Affine2>>& paths,
    const Affine2& parent_transform = Affine2(1.0f)) {
    for (const auto& [name, node] : tree) {
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
                paths.emplace_back(*d, transform);
            }
        } else if (name == "rect" || name == "circle" || name == "ellipse" ||
                   name == "line" || name == "polyline" || name == "polygon") {
            if (auto d = primitive_path_data(name, node); !d.empty()) {
                paths.emplace_back(std::move(d), transform);
            }
        }
        collect_path_data(node, paths, transform);
    }
}

} // namespace

SvgPathLoadResult parse_svg_path_data(std::string_view d, SvgPathLoadOptions options) {
    return SvgImporter{}.import_path_data(d, options);
}

SvgDocumentLoadResult load_svg_document_file(
    const std::string& filename,
    SvgPathLoadOptions options) {
    boost::property_tree::ptree document;
    try {
        boost::property_tree::read_xml(
            filename, document,
            boost::property_tree::xml_parser::trim_whitespace);
    } catch (const boost::property_tree::xml_parser::xml_parser_error& e) {
        return {.paths = {}, .ok = false, .error = e.what()};
    }

    std::vector<std::pair<std::string, Affine2>> path_data;
    collect_path_data(document, path_data);
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
        result.paths.push_back(std::move(path.path));
    }
    result.ok = true;
    return result;
}

} // namespace chronon3d::assets
