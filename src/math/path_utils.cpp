#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/math.hpp>
#include <numeric>
#include <algorithm>

namespace chronon3d::math {

std::vector<Vec2> flatten_cubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int segments) {
    std::vector<Vec2> out;
    out.reserve(segments + 1);

    for (int i = 0; i <= segments; ++i) {
        f32 t = static_cast<f32>(i) / static_cast<f32>(segments);
        f32 u = 1.0f - t;

        Vec2 p =
            u * u * u * p0 +
            3.0f * u * u * t * p1 +
            3.0f * u * t * t * p2 +
            t * t * t * p3;

        out.push_back(p);
    }

    return out;
}

std::vector<Vec2> flatten_quadratic(Vec2 p0, Vec2 p1, Vec2 p2, int segments) {
    std::vector<Vec2> out;
    out.reserve(segments + 1);

    for (int i = 0; i <= segments; ++i) {
        f32 t = static_cast<f32>(i) / static_cast<f32>(segments);
        f32 u = 1.0f - t;

        Vec2 p =
            u * u * p0 +
            2.0f * u * t * p1 +
            t * t * p2;

        out.push_back(p);
    }

    return out;
}

std::vector<std::vector<Vec2>> flatten_path(const PathShape& path, int segments) {
    std::vector<std::vector<Vec2>> subpaths;
    if (path.commands.empty()) return subpaths;

    std::vector<Vec2> current_points;
    Vec2 current_pos{0, 0};
    Vec2 start_pos{0, 0};

    for (const auto& cmd : path.commands) {
        switch (cmd.type) {
            case PathCommandType::MoveTo:
                if (!current_points.empty()) {
                    subpaths.push_back(std::move(current_points));
                    current_points = {};
                }
                current_pos = cmd.p0;
                start_pos = cmd.p0;
                current_points.push_back(current_pos);
                break;

            case PathCommandType::LineTo:
                current_pos = cmd.p0;
                current_points.push_back(current_pos);
                break;

            case PathCommandType::CubicTo: {
                auto cubic = flatten_cubic(current_pos, cmd.p1, cmd.p2, cmd.p0, segments);
                // Skip the first point as it is the same as current_pos
                current_points.insert(current_points.end(), cubic.begin() + 1, cubic.end());
                current_pos = cmd.p0;
                break;
            }

            case PathCommandType::QuadraticTo: {
                auto quad = flatten_quadratic(current_pos, cmd.p1, cmd.p0, segments);
                current_points.insert(current_points.end(), quad.begin() + 1, quad.end());
                current_pos = cmd.p0;
                break;
            }

            case PathCommandType::Close:
                if (current_pos != start_pos) {
                    current_points.push_back(start_pos);
                    current_pos = start_pos;
                }
                if (!current_points.empty()) {
                    subpaths.push_back(std::move(current_points));
                    current_points = {};
                }
                break;
        }
    }

    if (!current_points.empty() && path.closed) {
        if (current_points.front() != current_points.back()) {
            current_points.push_back(current_points.front());
        }
    }

    if (!current_points.empty()) {
        subpaths.push_back(std::move(current_points));
    }

    return subpaths;
}

f32 polyline_length(const std::vector<Vec2>& points) {
    if (points.size() < 2) return 0.0f;
    f32 length = 0.0f;
    for (size_t i = 1; i < points.size(); ++i) {
        length += glm::length(points[i] - points[i - 1]);
    }
    return length;
}

std::vector<Vec2> trim_polyline(const std::vector<Vec2>& points, f32 trim_start, f32 trim_end) {
    trim_start = std::clamp(trim_start, 0.0f, 1.0f);
    trim_end = std::clamp(trim_end, 0.0f, 1.0f);

    if (trim_end <= trim_start || points.size() < 2) return {};

    f32 total_length = polyline_length(points);
    if (total_length < 1e-6f) return points;

    f32 start_dist = trim_start * total_length;
    f32 end_dist = trim_end * total_length;

    std::vector<Vec2> out;
    f32 current_dist = 0.0f;
    bool started = false;

    for (size_t i = 1; i < points.size(); ++i) {
        Vec2 a = points[i - 1];
        Vec2 b = points[i];
        f32 seg_len = glm::length(b - a);
        f32 next_dist = current_dist + seg_len;

        if (!started) {
            if (next_dist >= start_dist) {
                f32 t = (seg_len > 1e-6f) ? (start_dist - current_dist) / seg_len : 0.0f;
                out.push_back(a + (b - a) * t);
                started = true;
            }
        }

        if (started) {
            if (next_dist >= end_dist) {
                f32 t = (seg_len > 1e-6f) ? (end_dist - current_dist) / seg_len : 1.0f;
                out.push_back(a + (b - a) * t);
                break;
            } else if (next_dist > start_dist) {
                out.push_back(b);
            }
        }

        current_dist = next_dist;
    }

    return out;
}

} // namespace chronon3d::math
