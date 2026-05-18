#pragma once

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/scene/path.hpp>
#include <vector>

namespace chronon3d::math {

/**
 * Flattens a cubic Bezier curve into a series of points.
 */
std::vector<Vec2> flatten_cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int segments = 20);

/**
 * Flattens a quadratic Bezier curve into a series of points.
 */
std::vector<Vec2> flatten_quadratic(Vec2 p0, Vec2 p1, Vec2 p2, int segments = 15);

/**
 * Converts a PathShape into a series of polylines (one per subpath).
 */
std::vector<std::vector<Vec2>> flatten_path(const PathShape& path, int segments = 20);

/**
 * Calculates the total length of a polyline.
 */
f32 polyline_length(const std::vector<Vec2>& points);

/**
 * Trims a polyline to a specific range [0..1] of its total length.
 */
std::vector<Vec2> trim_polyline(const std::vector<Vec2>& points, f32 trim_start, f32 trim_end);

} // namespace chronon3d::math
