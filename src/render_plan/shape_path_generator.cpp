#include <chronon3d/render_plan/shape_path_generator.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chronon3d::render_plan {
namespace {
constexpr float kPi = 3.14159265358979323846f;

ShapePathCommandPlan command(ShapePathCommandTypePlan type, float x = 0.0f, float y = 0.0f) {
    ShapePathCommandPlan result;
    result.type = type;
    result.point = {x, y};
    return result;
}

void validate_size(float width, float height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f)
        throw std::invalid_argument("procedural shape size must be positive and finite");
}

std::uint64_t grid_axis_count(float extent, float spacing) {
    const double count = std::floor(static_cast<double>(extent) /
                                    static_cast<double>(spacing)) + 1.0;
    if (!std::isfinite(count) || count < 1.0 ||
        count > static_cast<double>(kMaxGeneratedGridElements))
        throw std::length_error("generated grid axis count exceeds the 4096 element limit");
    return static_cast<std::uint64_t>(count);
}

std::vector<ShapePathCommandPlan> regular_path(
    float width, float height, std::uint32_t vertices, float rotation_degrees,
    float inner_ratio) {
    validate_size(width, height);
    if (vertices < 3 || vertices > 256 || !std::isfinite(rotation_degrees) ||
        !std::isfinite(inner_ratio) || inner_ratio <= 0.0f || inner_ratio > 1.0f)
        throw std::invalid_argument("procedural polygon parameters are outside supported bounds");
    std::vector<ShapePathCommandPlan> path;
    path.reserve(static_cast<std::size_t>(vertices) * (inner_ratio < 1.0f ? 2U : 1U) + 1U);
    const auto total = vertices * (inner_ratio < 1.0f ? 2U : 1U);
    for (std::uint32_t i = 0; i < total; ++i) {
        const float angle = (rotation_degrees - 90.0f + 360.0f * static_cast<float>(i) /
                             static_cast<float>(total)) * (kPi / 180.0f);
        const float radius = (i % 2U == 0U ? 1.0f : inner_ratio);
        const float x = std::cos(angle) * width * 0.5f * radius;
        const float y = std::sin(angle) * height * 0.5f * radius;
        path.push_back(command(i == 0 ? ShapePathCommandTypePlan::MoveTo
                                      : ShapePathCommandTypePlan::LineTo, x, y));
    }
    path.push_back(command(ShapePathCommandTypePlan::Close));
    return path;
}
} // namespace

std::vector<ShapePathCommandPlan> make_star_path(
    float width, float height, std::uint32_t points, float inner_ratio, float rotation_degrees) {
    if (inner_ratio >= 1.0f)
        throw std::invalid_argument("star inner_ratio must be in (0, 1)");
    return regular_path(width, height, points, rotation_degrees, inner_ratio);
}

std::vector<ShapePathCommandPlan> make_polygon_path(
    float width, float height, std::uint32_t points, float rotation_degrees) {
    return regular_path(width, height, points, rotation_degrees, 1.0f);
}

std::vector<ShapePathCommandPlan> make_arrow_path(
    float width, float height, float shaft_ratio, float head_ratio) {
    validate_size(width, height);
    if (!std::isfinite(shaft_ratio) || shaft_ratio <= 0.0f || shaft_ratio >= 1.0f ||
        !std::isfinite(head_ratio) || head_ratio <= 0.0f || head_ratio >= 1.0f)
        throw std::invalid_argument("arrow ratios must be finite and in (0, 1)");
    const float left = -width * 0.5f, right = width * 0.5f;
    const float shaft_half = height * shaft_ratio * 0.5f;
    const float head_half = height * head_ratio * 0.5f;
    const float neck = right - width * 0.36f;
    return {command(ShapePathCommandTypePlan::MoveTo, left, -shaft_half),
            command(ShapePathCommandTypePlan::LineTo, neck, -shaft_half),
            command(ShapePathCommandTypePlan::LineTo, neck, -head_half),
            command(ShapePathCommandTypePlan::LineTo, right, 0.0f),
            command(ShapePathCommandTypePlan::LineTo, neck, head_half),
            command(ShapePathCommandTypePlan::LineTo, neck, shaft_half),
            command(ShapePathCommandTypePlan::LineTo, left, shaft_half),
            command(ShapePathCommandTypePlan::Close)};
}

std::vector<ShapePathCommandPlan> make_arc_path(
    float width, float height, float start_degrees, float sweep_degrees, std::uint32_t segments) {
    validate_size(width, height);
    if (!std::isfinite(start_degrees) || !std::isfinite(sweep_degrees) ||
        std::abs(sweep_degrees) <= 0.0f || std::abs(sweep_degrees) > 360.0f ||
        segments < 1 || segments > 4096)
        throw std::invalid_argument("arc requires finite angles, non-zero sweep <= 360 degrees, and 1..4096 segments");
    const auto count = static_cast<std::uint32_t>(std::ceil(
        static_cast<float>(segments) * std::abs(sweep_degrees) / 360.0f));
    std::vector<ShapePathCommandPlan> path;
    path.reserve(static_cast<std::size_t>(count) + 1U);
    for (std::uint32_t i = 0; i <= count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float angle = (start_degrees + sweep_degrees * t - 90.0f) * (kPi / 180.0f);
        path.push_back(command(i == 0 ? ShapePathCommandTypePlan::MoveTo
                                      : ShapePathCommandTypePlan::LineTo,
                               std::cos(angle) * width * 0.5f,
                               std::sin(angle) * height * 0.5f));
    }
    return path;
}

std::vector<ShapePathCommandPlan> make_grid_path(float width, float height, float spacing) {
    validate_size(width, height);
    if (!std::isfinite(spacing) || spacing <= 0.0f)
        throw std::invalid_argument("grid spacing must be positive and finite");
    const auto columns = grid_axis_count(width, spacing);
    const auto rows = grid_axis_count(height, spacing);
    if (columns > kMaxGeneratedGridElements || rows > kMaxGeneratedGridElements ||
        columns + rows > kMaxGeneratedGridElements)
        throw std::length_error("generated grid line count exceeds 4096");
    std::vector<ShapePathCommandPlan> path;
    path.reserve(static_cast<std::size_t>((columns + rows) * 2U));
    for (std::uint64_t column = 0; column < columns; ++column) {
        const float x = -width * 0.5f + static_cast<float>(column) * spacing;
        path.push_back(command(ShapePathCommandTypePlan::MoveTo, x, -height * 0.5f));
        path.push_back(command(ShapePathCommandTypePlan::LineTo, x, height * 0.5f));
    }
    for (std::uint64_t row = 0; row < rows; ++row) {
        const float y = -height * 0.5f + static_cast<float>(row) * spacing;
        path.push_back(command(ShapePathCommandTypePlan::MoveTo, -width * 0.5f, y));
        path.push_back(command(ShapePathCommandTypePlan::LineTo, width * 0.5f, y));
    }
    return path;
}

std::vector<ShapePathCommandPlan> make_dot_grid_path(
    float width, float height, float spacing, float radius) {
    validate_size(width, height);
    if (!std::isfinite(spacing) || spacing <= 0.0f || !std::isfinite(radius) ||
        radius <= 0.0f || radius > spacing * 0.5f)
        throw std::invalid_argument("dot-grid requires positive spacing and radius <= spacing/2");
    const double available_width = std::max(0.0, static_cast<double>(width) - 2.0 * radius);
    const double available_height = std::max(0.0, static_cast<double>(height) - 2.0 * radius);
    const auto columns = grid_axis_count(static_cast<float>(available_width), spacing);
    const auto rows = grid_axis_count(static_cast<float>(available_height), spacing);
    if (columns > kMaxGeneratedGridElements || rows > kMaxGeneratedGridElements ||
        columns > kMaxGeneratedGridElements / rows)
        throw std::length_error("generated dot count exceeds 4096");
    const auto dots = columns * rows;
    std::vector<ShapePathCommandPlan> path;
    path.reserve(static_cast<std::size_t>(dots * 10U));
    for (std::uint64_t row = 0; row < rows; ++row) {
        const float cy = -height * 0.5f + radius + static_cast<float>(row) * spacing;
        for (std::uint64_t column = 0; column < columns; ++column) {
            const float cx = -width * 0.5f + radius + static_cast<float>(column) * spacing;
            for (std::uint32_t vertex = 0; vertex < 8; ++vertex) {
                const float angle = (static_cast<float>(vertex) * 45.0f) * (kPi / 180.0f);
                path.push_back(command(vertex == 0 ? ShapePathCommandTypePlan::MoveTo
                                                    : ShapePathCommandTypePlan::LineTo,
                                       cx + std::cos(angle) * radius,
                                       cy + std::sin(angle) * radius));
            }
            path.push_back(command(ShapePathCommandTypePlan::Close));
        }
    }
    return path;
}

} // namespace chronon3d::render_plan
