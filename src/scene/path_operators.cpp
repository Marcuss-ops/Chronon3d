#include <chronon3d/scene/model/shape/path.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chronon3d {
namespace {
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
PathShape round_corners(const PathShape& source, float radius) {
    if (!std::isfinite(radius) || radius < 0.0f)
        throw std::invalid_argument("round-corners radius must be finite and non-negative");
    if (radius == 0.0f) return source;
    std::vector<Vec2> points;
    bool closed = false;
    std::size_t subpaths = 0;
    for (const auto& command : source.commands) {
        if (command.type == PathCommandType::MoveTo) {
            if (++subpaths > 1)
                throw std::invalid_argument("round-corners currently requires a single subpath");
            points.push_back(command.p0);
        }
        else if (command.type == PathCommandType::LineTo) points.push_back(command.p0);
        else if (command.type == PathCommandType::Close) closed = true;
        else throw std::invalid_argument("round-corners currently requires a polyline path");
    }
    if (closed && points.size() > 1 && glm::length(points.front() - points.back()) <= 1e-6f)
        points.pop_back();
    if (points.size() < 3U) return source;
    PathShape output = source;
    output.commands.clear();
    const auto corner_points = [&](std::size_t i) {
        const std::size_t prev = (i + points.size() - 1) % points.size();
        const std::size_t next = (i + 1) % points.size();
        const Vec2 in = points[i] - points[prev];
        const Vec2 out = points[next] - points[i];
        const float in_len = glm::length(in), out_len = glm::length(out);
        if (in_len <= 1e-6f || out_len <= 1e-6f)
            return std::pair{points[i], points[i]};
        const float r = std::min({radius, in_len * 0.5f, out_len * 0.5f});
        return std::pair{points[i] - in * (r / in_len), points[i] + out * (r / out_len)};
    };
    if (closed) {
        output.commands.push_back(PathCommand::move_to(corner_points(0).first));
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto [incoming, outgoing] = corner_points(i);
            if (i != 0) output.commands.push_back(PathCommand::line_to(incoming));
            output.commands.push_back(PathCommand::quadratic_to(points[i], outgoing));
        }
        output.commands.push_back(PathCommand::close());
    } else {
        output.commands.push_back(PathCommand::move_to(points.front()));
        for (std::size_t i = 1; i + 1 < points.size(); ++i) {
            const auto [incoming, outgoing] = corner_points(i);
            output.commands.push_back(PathCommand::line_to(incoming));
            output.commands.push_back(PathCommand::quadratic_to(points[i], outgoing));
        }
        output.commands.push_back(PathCommand::line_to(points.back()));
    }
    output.closed = closed;
    return output;
}
}
std::vector<PathShape> apply_path_operators(const PathShape& input,
    const std::vector<PathOperatorSpec>& operators) {
    std::vector<PathShape> paths{input};
    for (const auto& op : operators) {
        if (op.kind == PathOperatorKind::RoundCorners) {
            if (!std::holds_alternative<RoundCornersParams>(op.params))
                throw std::invalid_argument("round-corners parameters do not match operator kind");
            for (auto& path : paths)
                path = round_corners(path, std::get<RoundCornersParams>(op.params).radius);
        } else if (op.kind == PathOperatorKind::Repeater) {
            if (!std::holds_alternative<RepeaterParams>(op.params))
                throw std::invalid_argument("repeater parameters do not match operator kind");
            const auto& r = std::get<RepeaterParams>(op.params);
            if (r.copies == 0 || r.copies > 10000 || !std::isfinite(r.opacity_decay) || r.opacity_decay < 0.0f)
                throw std::invalid_argument("invalid repeater parameters");
            std::vector<PathShape> expanded;
            expanded.reserve(paths.size() * r.copies);
            for (const auto& source : paths) for (std::uint32_t i = 0; i < r.copies; ++i) {
                PathShape copy = source;
                for (auto& command : copy.commands) {
                    if (command.type == PathCommandType::Close) continue;
                    transform_point(command.p0, r, i);
                    if (command.type == PathCommandType::CubicTo || command.type == PathCommandType::QuadraticTo)
                        transform_point(command.p1, r, i);
                    if (command.type == PathCommandType::CubicTo) transform_point(command.p2, r, i);
                }
                multiply_opacity(copy, std::pow(r.opacity_decay, static_cast<float>(i)));
                expanded.push_back(std::move(copy));
            }
            paths = std::move(expanded);
        } else {
            throw std::invalid_argument("unsupported path operator kind");
        }
    }
    return paths;
}
} // namespace chronon3d
