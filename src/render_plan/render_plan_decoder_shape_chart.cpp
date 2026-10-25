#include "render_plan_decoder_detail.hpp"

#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/render_plan/shape_path_generator.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace chronon3d::render_plan::detail {
namespace {

using json = nlohmann::json;
using Command = ShapePathCommandPlan;
using CommandType = ShapePathCommandTypePlan;

Command move_to(float x, float y) {
    Command value;
    value.type = CommandType::MoveTo;
    value.point = {x, y};
    return value;
}

Command line_to(float x, float y) {
    Command value;
    value.type = CommandType::LineTo;
    value.point = {x, y};
    return value;
}

std::array<float, 4> color(const json& value, const char* name,
                           std::array<float, 4> fallback) {
    if (!value.contains(name)) return fallback;
    const auto raw = value.at(name).get<std::string>();
    const auto parsed = parse_hex_color(raw);
    if (!parsed) throw std::runtime_error(std::string("chart.") + name + " must be #RRGGBB");
    return {parsed->r, parsed->g, parsed->b, parsed->a};
}

float finite_number(const json& value, const char* name, float fallback,
                    float minimum, float maximum) {
    if (!value.contains(name)) return fallback;
    const float result = value.at(name).get<float>();
    if (!std::isfinite(result) || result < minimum || result > maximum)
        throw std::runtime_error(std::string("chart.") + name + " is outside its finite bounds");
    return result;
}

std::array<float, 2> domain(const json& value, const char* name,
                            float minimum, float maximum) {
    if (!value.contains(name)) return {minimum, maximum};
    const auto& raw = value.at(name);
    if (!raw.is_array() || raw.size() != 2 ||
        !raw[0].is_number() || !raw[1].is_number())
        throw std::runtime_error(std::string("chart.") + name + " must contain two numbers");
    std::array<float, 2> result{raw[0].get<float>(), raw[1].get<float>()};
    if (!std::isfinite(result[0]) || !std::isfinite(result[1]) ||
        result[0] < -100000.0f || result[1] > 100000.0f ||
        !(result[1] > result[0]))
        throw std::runtime_error(std::string("chart.") + name + " must be finite and increasing");
    return result;
}

ImportedSubpathPlan stroked(std::vector<Command> path,
                            std::array<float, 4> rgba, float width) {
    ImportedSubpathPlan subpath;
    subpath.path = std::move(path);
    StrokeStyle style;
    style.width = width;
    style.solid_color = rgba;
    style.cap = "butt";
    style.join = "miter";
    subpath.stroke = std::move(style);
    return subpath;
}

} // namespace

void decode_monotone_chart(LayerPlan& layer, ShapeContentPlan& shape,
                           const nlohmann::json& raw) {
    if (raw.contains("path") || raw.contains("asset") || raw.contains("lottie") ||
        raw.contains("fill") || raw.contains("stroke"))
        throw std::runtime_error("monotone_chart uses data_points and chart style fields only");
    const auto& raw_points = raw.at("data_points");
    if (!raw_points.is_array() || raw_points.size() < 2 || raw_points.size() > 4096)
        throw std::runtime_error("monotone_chart data_points must contain 2..4096 [x,y] pairs");
    std::vector<std::array<float, 2>> points;
    points.reserve(raw_points.size());
    float ymin = std::numeric_limits<float>::infinity();
    float ymax = -std::numeric_limits<float>::infinity();
    for (const auto& point : raw_points) {
        if (!point.is_array() || point.size() != 2 ||
            !point[0].is_number() || !point[1].is_number())
            throw std::runtime_error("monotone_chart points must be numeric [x,y] pairs");
        const float x = point[0].get<float>();
        const float y = point[1].get<float>();
        if (!std::isfinite(x) || !std::isfinite(y) ||
            std::abs(x) > 100000.0f || std::abs(y) > 100000.0f)
            throw std::runtime_error("monotone_chart points must be finite and within [-100000,100000]");
        if (!points.empty() && !(x > points.back()[0]))
            throw std::runtime_error("monotone_chart x values must be strictly increasing");
        points.push_back({x, y});
        ymin = std::min(ymin, y);
        ymax = std::max(ymax, y);
    }
    const auto x_domain = domain(raw, "x_domain", points.front()[0], points.back()[0]);
    if (x_domain[0] > points.front()[0] || x_domain[1] < points.back()[0])
        throw std::runtime_error("monotone_chart x_domain must contain every data point");
    if (!(ymax > ymin)) {
        const float pad = std::max(1.0f, std::abs(ymin) * 0.05f);
        ymin -= pad;
        ymax += pad;
    }
    const auto y_domain = domain(raw, "y_domain", ymin, ymax);
    for (const auto& point : points)
        if (point[1] < y_domain[0] || point[1] > y_domain[1])
            throw std::runtime_error("monotone_chart y_domain must contain every data point");

    const float width = layer.size[0], height = layer.size[1];
    const std::array<float, 4> default_insets{
        std::min(12.0f, height * 0.15f), std::min(12.0f, width * 0.10f),
        std::min(16.0f, height * 0.20f), std::min(28.0f, width * 0.25f)};
    auto insets = default_insets;
    if (raw.contains("plot_insets")) {
        const auto& value = raw.at("plot_insets");
        if (!value.is_array() || value.size() != 4)
            throw std::runtime_error("monotone_chart plot_insets must be [top,right,bottom,left]");
        for (std::size_t i = 0; i < insets.size(); ++i) {
            insets[i] = value[i].get<float>();
            if (!std::isfinite(insets[i]) || insets[i] < 0.0f || insets[i] > 2048.0f)
                throw std::runtime_error("monotone_chart plot_insets must be within [0,2048]");
        }
    }
    const float plot_width = width - insets[1] - insets[3];
    const float plot_height = height - insets[0] - insets[2];
    if (!(plot_width > 1.0f) || !(plot_height > 1.0f))
        throw std::runtime_error("monotone_chart insets leave no drawable plot area");
    const float left = -width * 0.5f + insets[3];
    const float right = left + plot_width;
    const float top = -height * 0.5f + insets[0];
    const float bottom = top + plot_height;
    for (auto& point : points) {
        point[0] = left + (point[0] - x_domain[0]) /
            (x_domain[1] - x_domain[0]) * plot_width;
        point[1] = bottom - (point[1] - y_domain[0]) /
            (y_domain[1] - y_domain[0]) * plot_height;
    }

    const auto area_rgba = color(raw, "area_color", {0.16f, 0.38f, 0.72f, 0.28f});
    const auto line_rgba = color(raw, "line_color", {0.12f, 0.34f, 0.78f, 1.0f});
    const auto grid_rgba = color(raw, "grid_color", {0.68f, 0.71f, 0.76f, 0.45f});
    const auto axis_rgba = color(raw, "axis_color", {0.22f, 0.24f, 0.29f, 1.0f});
    const float line_width = finite_number(raw, "line_width", 2.0f, 0.25f, 64.0f);
    const float grid_width = finite_number(raw, "grid_width", 1.0f, 0.25f, 16.0f);
    const float axis_width = finite_number(raw, "axis_width", 1.5f, 0.25f, 32.0f);
    const float tick_size = finite_number(raw, "tick_size", 5.0f, 0.0f, 64.0f);
    const auto x_ticks = raw.value("x_ticks", 6U);
    const auto y_ticks = raw.value("y_ticks", 5U);
    if (x_ticks < 2 || x_ticks > 64 || y_ticks < 2 || y_ticks > 64)
        throw std::runtime_error("monotone_chart x_ticks and y_ticks must be within [2,64]");

    std::optional<ImportedSubpathPlan> grid_subpath;
    if (raw.value("show_grid", true)) {
        std::vector<Command> grid;
        grid.reserve(static_cast<std::size_t>(x_ticks + y_ticks) * 2U);
        for (std::uint32_t i = 1; i + 1 < x_ticks; ++i) {
            const float x = left + plot_width * static_cast<float>(i) /
                static_cast<float>(x_ticks - 1);
            grid.push_back(move_to(x, top));
            grid.push_back(line_to(x, bottom));
        }
        for (std::uint32_t i = 1; i + 1 < y_ticks; ++i) {
            const float y = top + plot_height * static_cast<float>(i) /
                static_cast<float>(y_ticks - 1);
            grid.push_back(move_to(left, y));
            grid.push_back(line_to(right, y));
        }
        grid_subpath = stroked(std::move(grid), grid_rgba, grid_width);
    }
    const auto line = make_monotone_cubic_path(points);
    if (grid_subpath) shape.imported_subpaths.push_back(std::move(*grid_subpath));
    ImportedSubpathPlan area;
    area.path = line;
    area.path.push_back(line_to(points.back()[0], bottom));
    area.path.push_back(line_to(points.front()[0], bottom));
    Command close;
    close.type = CommandType::Close;
    area.path.push_back(close);
    area.fill_color = area_rgba;
    shape.imported_subpaths.push_back(std::move(area));
    shape.imported_subpaths.push_back(stroked(line, line_rgba, line_width));
    if (raw.value("show_axes", true)) {
        std::vector<Command> axes{
            move_to(left, bottom), line_to(right, bottom),
            move_to(left, bottom), line_to(left, top)};
        for (std::uint32_t i = 0; i < x_ticks; ++i) {
            const float x = left + plot_width * static_cast<float>(i) /
                static_cast<float>(x_ticks - 1);
            axes.push_back(move_to(x, bottom));
            axes.push_back(line_to(x, bottom + tick_size));
        }
        for (std::uint32_t i = 0; i < y_ticks; ++i) {
            const float y = top + plot_height * static_cast<float>(i) /
                static_cast<float>(y_ticks - 1);
            axes.push_back(move_to(left - tick_size, y));
            axes.push_back(line_to(left, y));
        }
        shape.imported_subpaths.push_back(stroked(std::move(axes), axis_rgba, axis_width));
    }
}

} // namespace chronon3d::render_plan::detail
