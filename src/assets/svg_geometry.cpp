#include "svg_gradient_detail.hpp"

#include <cmath>
#include <algorithm>
#include <iomanip>
#include <regex>
#include <sstream>

namespace chronon3d::assets::svg_gradient_detail {
using svg_loader_detail::parse_svg_length;
using svg_loader_detail::transform_numbers;

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
std::string primitive_path_data(
    const std::string& name,
    const boost::property_tree::ptree& node) {
    const auto attr = [&node](const char* key, float fallback = 0.0f) {
        if (const auto opt = node.get_optional<std::string>(std::string{"<xmlattr>."} + key)) {
            if (const auto parsed = parse_svg_length(*opt)) return *parsed;
        }
        return fallback;
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
}  // namespace chronon3d::assets::svg_gradient_detail
