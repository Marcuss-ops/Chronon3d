#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/animation/effects/wiggle.hpp>
#include "path_operators_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace chronon3d::path_operator_detail {

double point_line_distance(const Vec2& point, const Vec2& start, const Vec2& end) {
    const double dx = static_cast<double>(end.x) - start.x;
    const double dy = static_cast<double>(end.y) - start.y;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared <= 1.0e-24)
        return std::hypot(static_cast<double>(point.x) - start.x,
                          static_cast<double>(point.y) - start.y);
    const double projection = std::clamp(
        ((static_cast<double>(point.x) - start.x) * dx +
         (static_cast<double>(point.y) - start.y) * dy) / length_squared, 0.0, 1.0);
    return std::hypot(static_cast<double>(point.x) - (start.x + projection * dx),
                      static_cast<double>(point.y) - (start.y + projection * dy));
}

void flatten_cubic_adaptive(const Vec2& p0, const Vec2& p1, const Vec2& p2,
                            const Vec2& p3, int depth,
                            std::vector<Clipper2Lib::PointD>& out) {
    if (out.size() >= kPathGeometryLimit)
        throw std::invalid_argument("path curve flattening exceeds the geometry limit");
    const double deviation = std::max(point_line_distance(p1, p0, p3),
                                      point_line_distance(p2, p0, p3));
    if (deviation <= kPathCurveTolerance || depth >= 16) {
        out.emplace_back(p3.x, p3.y);
        return;
    }
    const Vec2 p01 = (p0 + p1) * 0.5f;
    const Vec2 p12 = (p1 + p2) * 0.5f;
    const Vec2 p23 = (p2 + p3) * 0.5f;
    const Vec2 p012 = (p01 + p12) * 0.5f;
    const Vec2 p123 = (p12 + p23) * 0.5f;
    const Vec2 midpoint = (p012 + p123) * 0.5f;
    flatten_cubic_adaptive(p0, p01, p012, midpoint, depth + 1, out);
    flatten_cubic_adaptive(midpoint, p123, p23, p3, depth + 1, out);
}

std::vector<FlattenedContour> flatten_path_for_geometry(const PathShape& path) {
    std::vector<FlattenedContour> result;
    FlattenedContour contour;
    Vec2 current{};
    Vec2 start{};
    bool active = false;
    std::size_t total_points = 0;
    auto finish = [&]() {
        if (active && !contour.points.empty()) {
            if (contour.points.size() > kPathGeometryLimit - total_points)
                throw std::invalid_argument("path flattening exceeds the geometry limit");
            total_points += contour.points.size();
            result.push_back(std::move(contour));
        }
        contour = {};
        active = false;
    };
    const auto close_contour = [&] {
        contour.closed = true;
        if (contour.points.size() > 1 && contour.points.front() != contour.points.back())
            contour.points.push_back(contour.points.front());
    };
    const auto add_point = [&](const Vec2& point) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            std::abs(static_cast<double>(point.x)) > 1.0e10 ||
            std::abs(static_cast<double>(point.y)) > 1.0e10)
            throw std::invalid_argument("path coordinates must be finite and within the geometry range");
    };
    for (const auto& command : path.commands) {
        if (command.type == PathCommandType::MoveTo) {
            finish();
            add_point(command.p0);
            current = start = command.p0;
            contour.points.emplace_back(current.x, current.y);
            active = true;
            continue;
        }
        if (command.type == PathCommandType::Close) {
            if (active) {
                close_contour();
                current = start;
                finish();
            }
            continue;
        }
        if (!active) throw std::invalid_argument("path drawing command has no active contour");
        add_point(command.p0);
        if (command.type == PathCommandType::LineTo) {
            contour.points.emplace_back(command.p0.x, command.p0.y);
        } else if (command.type == PathCommandType::QuadraticTo) {
            add_point(command.p1);
            const Vec2 c1 = current + (command.p1 - current) * (2.0f / 3.0f);
            const Vec2 c2 = command.p0 + (command.p1 - command.p0) * (2.0f / 3.0f);
            flatten_cubic_adaptive(current, c1, c2, command.p0, 0, contour.points);
        } else if (command.type == PathCommandType::CubicTo) {
            add_point(command.p1);
            add_point(command.p2);
            flatten_cubic_adaptive(current, command.p1, command.p2, command.p0, 0,
                                   contour.points);
        }
        current = command.p0;
    }
    if (active && path.closed) close_contour();
    finish();
    return result;
}

Clipper2Lib::FillRule clipper_fill_rule(PathFillRule rule) {
    return rule == PathFillRule::EvenOdd ? Clipper2Lib::FillRule::EvenOdd
                                         : Clipper2Lib::FillRule::NonZero;
}

Clipper2Lib::PathsD closed_paths(const std::vector<FlattenedContour>& contours,
                                bool close_open_contours) {
    Clipper2Lib::PathsD paths;
    for (const auto& contour : contours) {
        if ((!contour.closed && !close_open_contours) || contour.points.size() < 3) continue;
        auto points = contour.points;
        if (points.front() == points.back()) points.pop_back();
        if (points.size() >= 3) paths.push_back(std::move(points));
    }
    return paths;
}

void append_closed_paths(std::vector<PathCommand>& commands,
                         const Clipper2Lib::PathsD& paths) {
    std::size_t total = commands.size();
    for (const auto& path : paths) {
        if (path.size() < 3) continue;
        if (path.size() + 2 > kPathGeometryLimit - total)
            throw std::invalid_argument("path operation exceeds the geometry limit");
        total += path.size() + 2;
        commands.push_back(PathCommand::move_to({static_cast<float>(path.front().x),
                                                 static_cast<float>(path.front().y)}));
        for (std::size_t index = 1; index < path.size(); ++index)
            commands.push_back(PathCommand::line_to({static_cast<float>(path[index].x),
                                                     static_cast<float>(path[index].y)}));
        commands.push_back(PathCommand::close());
    }
}

void append_offset_open_contour(std::vector<PathCommand>& commands,
                                const FlattenedContour& contour, double amount,
                                double miter_limit) {
    if (contour.points.size() < 2) return;
    std::vector<Vec2> points;
    points.reserve(contour.points.size());
    for (const auto& point : contour.points)
        points.emplace_back(static_cast<float>(point.x), static_cast<float>(point.y));
    const std::size_t segment_count = points.size() - 1;
    std::vector<Vec2> normals;
    normals.reserve(segment_count);
    for (std::size_t index = 0; index < segment_count; ++index) {
        const Vec2 delta = points[index + 1] - points[index];
        const float length = glm::length(delta);
        if (length <= 1.0e-6f) continue;
        normals.emplace_back(-delta.y / length, delta.x / length);
    }
    if (normals.size() != segment_count) {
        points.clear();
        for (const auto& point : contour.points) {
            const Vec2 value{static_cast<float>(point.x), static_cast<float>(point.y)};
            if (points.empty() || glm::length(value - points.back()) > 1.0e-6f)
                points.push_back(value);
        }
        if (points.size() < 2) return;
        normals.clear();
        for (std::size_t index = 0; index + 1 < points.size(); ++index) {
            const Vec2 delta = points[index + 1] - points[index];
            const float length = glm::length(delta);
            if (length > 1.0e-6f) normals.emplace_back(-delta.y / length, delta.x / length);
        }
    }
    const auto offset_vertex = [&](std::size_t vertex) {
        if (vertex == 0) return points[vertex] + normals.front() * static_cast<float>(amount);
        if (vertex + 1 == points.size()) return points[vertex] + normals.back() * static_cast<float>(amount);
        const Vec2 previous = normals[vertex - 1];
        const Vec2 next = normals[vertex];
        const Vec2 sum = previous + next;
        const float sum_length = glm::length(sum);
        if (sum_length <= 1.0e-6f) return points[vertex] + next * static_cast<float>(amount);
        const Vec2 miter = sum / sum_length;
        const float denominator = glm::dot(miter, next);
        float distance = std::abs(denominator) > 1.0e-6f
            ? static_cast<float>(amount) / denominator
            : static_cast<float>(amount * miter_limit);
        distance = std::clamp(distance, -static_cast<float>(std::abs(amount) * miter_limit),
                              static_cast<float>(std::abs(amount) * miter_limit));
        return points[vertex] + miter * distance;
    };
    commands.push_back(PathCommand::move_to(offset_vertex(0)));
    for (std::size_t index = 1; index < points.size(); ++index)
        commands.push_back(PathCommand::line_to(offset_vertex(index)));
}

void transform_point(Vec2& p, const RepeaterParams& r, std::uint32_t copy) {
    const float t = static_cast<float>(copy);
    const float angle = glm::radians(r.rotation_degrees * t);
    const float c = std::cos(angle), s = std::sin(angle);
    Vec2 q = (p - r.anchor) * glm::pow(r.scale, Vec2(t));
    p = Vec2{q.x * c - q.y * s, q.x * s + q.y * c} + r.anchor + r.position * t;
}
void multiply_opacity(PathShape& p, float factor) {
    p.opacity *= factor;
    p.stroke.color.a *= factor;
    p.fill.solid.a *= factor;
    for (auto& stop : p.fill.gradient.color_stops) stop.color.a *= factor;
    for (auto& stop : p.fill.gradient.opacity_stops) stop.opacity *= factor;
    if (p.stroke.gradient) {
        for (auto& stop : p.stroke.gradient->color_stops) stop.color.a *= factor;
        for (auto& stop : p.stroke.gradient->opacity_stops) stop.opacity *= factor;
    }
}

} // namespace chronon3d::path_operator_detail
