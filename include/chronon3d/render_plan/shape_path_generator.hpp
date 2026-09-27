#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <cstdint>
#include <vector>

namespace chronon3d::render_plan {

inline constexpr std::uint64_t kMaxGeneratedGridElements = 4096;

[[nodiscard]] std::vector<ShapePathCommandPlan> make_star_path(
    float width, float height, std::uint32_t points, float inner_ratio, float rotation_degrees);
[[nodiscard]] std::vector<ShapePathCommandPlan> make_polygon_path(
    float width, float height, std::uint32_t points, float rotation_degrees);
[[nodiscard]] std::vector<ShapePathCommandPlan> make_arrow_path(
    float width, float height, float shaft_ratio, float head_ratio);
[[nodiscard]] std::vector<ShapePathCommandPlan> make_arc_path(
    float width, float height, float start_degrees, float sweep_degrees,
    std::uint32_t segments);
[[nodiscard]] std::vector<ShapePathCommandPlan> make_grid_path(
    float width, float height, float spacing);
[[nodiscard]] std::vector<ShapePathCommandPlan> make_dot_grid_path(
    float width, float height, float spacing, float radius);

} // namespace chronon3d::render_plan
