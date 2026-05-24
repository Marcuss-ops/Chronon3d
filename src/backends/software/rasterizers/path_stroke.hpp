#pragma once

#include <chronon3d/math/vec2.hpp>
#include <vector>

namespace chronon3d::renderer {

f32 segment_coverage(Vec2 p, Vec2 a, Vec2 b, f32 radius);

std::vector<Vec2> trim_polyline_points(const std::vector<Vec2>& points, bool closed, f32 start_t, f32 end_t);

} // namespace chronon3d::renderer
