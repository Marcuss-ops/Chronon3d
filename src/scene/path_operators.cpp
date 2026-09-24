#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/animation/effects/wiggle.hpp>
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
PathShape zig_zag(const PathShape& source, const ZigZagParams& params) {
    if (!std::isfinite(params.size) || params.size < 0.0f ||
        !std::isfinite(params.points_per_unit) || params.points_per_unit <= 0.0f)
        throw std::invalid_argument("zig-zag size and points_per_unit must be finite and valid");
    if (params.size == 0.0f) return source;
    std::vector<Vec2> points;
    bool closed = false;
    std::size_t subpaths = 0;
    for (const auto& command : source.commands) {
        if (command.type == PathCommandType::MoveTo) {
            if (++subpaths > 1) throw std::invalid_argument("zig-zag currently requires a single subpath");
            points.push_back(command.p0);
        } else if (command.type == PathCommandType::LineTo) points.push_back(command.p0);
        else if (command.type == PathCommandType::Close) closed = true;
        else throw std::invalid_argument("zig-zag currently requires a polyline path");
    }
    if (points.size() < 2) return source;
    const std::size_t segment_count = points.size() - 1 + (closed ? 1U : 0U);
    PathShape output = source;
    output.commands.clear();
    output.commands.push_back(PathCommand::move_to(points.front()));
    for (std::size_t i = 0; i < segment_count; ++i) {
        const Vec2 a = points[i];
        const Vec2 b = points[(i + 1) % points.size()];
        const Vec2 delta = b - a;
        const float length = glm::length(delta);
        if (length <= 1e-6f) continue;
        const Vec2 normal{-delta.y / length, delta.x / length};
        const float requested_peaks = std::ceil(length * params.points_per_unit);
        if (!std::isfinite(requested_peaks) || requested_peaks > 250000.0f)
            throw std::invalid_argument("zig-zag expansion exceeds the geometry limit");
        const auto peaks = static_cast<std::size_t>(std::max(1.0f, requested_peaks));
        const std::size_t steps = peaks * 4;
        for (std::size_t j = 1; j <= steps; ++j) {
            if (closed && i + 1 == segment_count && j == steps) continue;
            const float t = static_cast<float>(j) / static_cast<float>(steps);
            const float phase = static_cast<float>(peaks) * t;
            float wave = 0.0f;
            if (params.smooth) {
                wave = std::sin(6.2831853071795864769f * phase);
            } else {
                float fractional = phase - std::floor(phase);
                fractional = fractional - std::floor(fractional + 0.25f) + 0.25f;
                wave = 1.0f - 4.0f * std::abs(fractional - 0.5f);
            }
            const Vec2 point = a + delta * t + normal * (params.size * wave);
            output.commands.push_back(PathCommand::line_to(point));
            if (output.commands.size() > 1000000)
                throw std::invalid_argument("zig-zag expansion exceeds the geometry limit");
        }
    }
    if (closed) output.commands.push_back(PathCommand::close());
    output.closed = closed;
    return output;
}
PathShape offset_path(const PathShape& source, const OffsetPathParams& params) {
    if (!std::isfinite(params.amount) || !std::isfinite(params.miter_limit) ||
        params.miter_limit < 1.0f)
        throw std::invalid_argument("offset amount must be finite and miter_limit must be at least one");
    if (params.amount == 0.0f) return source;
    std::vector<Vec2> points;
    bool closed = false;
    std::size_t subpaths = 0;
    for (const auto& command : source.commands) {
        if (command.type == PathCommandType::MoveTo) {
            if (++subpaths > 1) throw std::invalid_argument("offset-path currently requires a single subpath");
            points.push_back(command.p0);
        } else if (command.type == PathCommandType::LineTo) points.push_back(command.p0);
        else if (command.type == PathCommandType::Close) closed = true;
        else throw std::invalid_argument("offset-path currently requires a polyline path");
    }
    if (closed && points.size() > 1 && glm::length(points.front() - points.back()) <= 1e-6f)
        points.pop_back();
    if (points.size() < 2) return source;
    const std::size_t segments = points.size() - 1 + (closed ? 1U : 0U);
    std::vector<Vec2> normals;
    normals.reserve(segments);
    for (std::size_t i = 0; i < segments; ++i) {
        const Vec2 delta = points[(i + 1) % points.size()] - points[i];
        const float len = glm::length(delta);
        if (len <= 1e-6f) throw std::invalid_argument("offset-path cannot process zero-length segments");
        normals.emplace_back(-delta.y / len, delta.x / len);
    }
    PathShape output = source;
    output.commands.clear();
    const auto offset_vertex = [&](std::size_t vertex) {
        if (!closed && vertex == 0) return points[vertex] + normals.front() * params.amount;
        if (!closed && vertex + 1 == points.size()) return points[vertex] + normals.back() * params.amount;
        const Vec2 previous = normals[(vertex + segments - 1) % segments];
        const Vec2 next = normals[vertex % segments];
        const Vec2 sum = previous + next;
        const float sum_len = glm::length(sum);
        if (sum_len <= 1e-6f) return points[vertex] + next * params.amount;
        const Vec2 miter = sum / sum_len;
        const float denominator = glm::dot(miter, next);
        float distance = std::abs(denominator) > 1e-6f
            ? params.amount / denominator : params.amount * params.miter_limit;
        const float maximum = std::abs(params.amount) * params.miter_limit;
        distance = std::clamp(distance, -maximum, maximum);
        return points[vertex] + miter * distance;
    };
    output.commands.push_back(PathCommand::move_to(offset_vertex(0)));
    for (std::size_t i = 1; i < points.size(); ++i)
        output.commands.push_back(PathCommand::line_to(offset_vertex(i)));
    if (closed) output.commands.push_back(PathCommand::close());
    output.closed = closed;
    return output;
}
PathShape pucker_bloat(const PathShape& source, const PuckerBloatParams& params) {
    if (!std::isfinite(params.amount) || params.amount < -1.0f || params.amount > 1.0f ||
        !std::isfinite(params.anchor.x) || !std::isfinite(params.anchor.y))
        throw std::invalid_argument("pucker-bloat amount must be in [-1, 1] and anchor finite");
    PathShape output = source;
    const float scale = 1.0f + params.amount;
    const auto deform = [&](Vec2& point) { point = params.anchor + (point - params.anchor) * scale; };
    for (auto& command : output.commands) {
        if (command.type == PathCommandType::Close) continue;
        deform(command.p0);
        if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
            deform(command.p1);
        if (command.type == PathCommandType::CubicTo) deform(command.p2);
    }
    return output;
}
PathShape twist_path(const PathShape& source, const TwistParams& params) {
    if (!std::isfinite(params.degrees) || !std::isfinite(params.center.x) ||
        !std::isfinite(params.center.y))
        throw std::invalid_argument("twist degrees and center must be finite");
    float max_radius = 0.0f;
    const auto include_radius = [&](const Vec2& p) {
        max_radius = std::max(max_radius, glm::length(p - params.center));
    };
    for (const auto& command : source.commands) {
        if (command.type == PathCommandType::Close) continue;
        include_radius(command.p0);
        if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
            include_radius(command.p1);
        if (command.type == PathCommandType::CubicTo) include_radius(command.p2);
    }
    if (max_radius <= 1e-6f || params.degrees == 0.0f) return source;
    PathShape output = source;
    const auto twist = [&](Vec2& p) {
        const Vec2 delta = p - params.center;
        const float angle = glm::radians(params.degrees * glm::length(delta) / max_radius);
        const float c = std::cos(angle), s = std::sin(angle);
        p = params.center + Vec2{delta.x * c - delta.y * s, delta.x * s + delta.y * c};
    };
    for (auto& command : output.commands) {
        if (command.type == PathCommandType::Close) continue;
        twist(command.p0);
        if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
            twist(command.p1);
        if (command.type == PathCommandType::CubicTo) twist(command.p2);
    }
    return output;
}
PathShape wiggle_path(const PathShape& source, const WigglePathParams& params,
                      std::uint32_t seed) {
    if (!std::isfinite(params.amount) || params.amount < 0.0f ||
        !std::isfinite(params.frequency) || params.frequency <= 0.0f)
        throw std::invalid_argument("wiggle-path amount and frequency must be finite and valid");
    PathShape output = source;
    std::uint32_t vertex_index = 0;
    const auto displace = [&](Vec2& point, std::uint32_t index) {
        const float coordinate = static_cast<float>(index) * params.frequency;
        point.x += detail::smooth_noise(coordinate, seed) * params.amount;
        point.y += detail::smooth_noise(coordinate, seed + 0x9e3779b9u) * params.amount;
    };
    for (auto& command : output.commands) {
        if (command.type == PathCommandType::Close) continue;
        displace(command.p0, vertex_index++);
        if (command.type == PathCommandType::QuadraticTo || command.type == PathCommandType::CubicTo)
            displace(command.p1, vertex_index++);
        if (command.type == PathCommandType::CubicTo) displace(command.p2, vertex_index++);
    }
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
        } else if (op.kind == PathOperatorKind::ZigZag) {
            if (!std::holds_alternative<ZigZagParams>(op.params))
                throw std::invalid_argument("zig-zag parameters do not match operator kind");
            for (auto& path : paths) path = zig_zag(path, std::get<ZigZagParams>(op.params));
        } else if (op.kind == PathOperatorKind::OffsetPath) {
            if (!std::holds_alternative<OffsetPathParams>(op.params))
                throw std::invalid_argument("offset-path parameters do not match operator kind");
            for (auto& path : paths) path = offset_path(path, std::get<OffsetPathParams>(op.params));
        } else if (op.kind == PathOperatorKind::PuckerBloat) {
            if (!std::holds_alternative<PuckerBloatParams>(op.params))
                throw std::invalid_argument("pucker-bloat parameters do not match operator kind");
            for (auto& path : paths) path = pucker_bloat(path, std::get<PuckerBloatParams>(op.params));
        } else if (op.kind == PathOperatorKind::Twist) {
            if (!std::holds_alternative<TwistParams>(op.params))
                throw std::invalid_argument("twist parameters do not match operator kind");
            for (auto& path : paths) path = twist_path(path, std::get<TwistParams>(op.params));
        } else if (op.kind == PathOperatorKind::WigglePath) {
            if (!std::holds_alternative<WigglePathParams>(op.params))
                throw std::invalid_argument("wiggle-path parameters do not match operator kind");
            for (auto& path : paths)
                path = wiggle_path(path, std::get<WigglePathParams>(op.params), op.seed);
        } else if (op.kind == PathOperatorKind::Merge) {
            if (!std::holds_alternative<MergeParams>(op.params))
                throw std::invalid_argument("merge parameters do not match operator kind");
            const auto& merge = std::get<MergeParams>(op.params);
            std::size_t appended = 0;
            for (const auto& subpath : merge.paths) {
                if (subpath.empty() || subpath.front().type != PathCommandType::MoveTo)
                    throw std::invalid_argument("each merged subpath must begin with move_to");
                if (subpath.size() > 1000000 - appended)
                    throw std::invalid_argument("compound path exceeds the geometry limit");
                appended += subpath.size();
            }
            for (auto& path : paths) {
                if (path.commands.size() > 1000000 - appended)
                    throw std::invalid_argument("compound path exceeds the geometry limit");
                for (const auto& subpath : merge.paths)
                    path.commands.insert(path.commands.end(), subpath.begin(), subpath.end());
            }
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
