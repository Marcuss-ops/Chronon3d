#include "path_operators_internal.hpp"
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/animation/effects/wiggle.hpp>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace chronon3d::path_operator_detail {
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
        params.miter_limit < 1.0f || params.miter_limit > 1000.0f ||
        std::abs(params.amount) > 1.0e10f)
        throw std::invalid_argument("offset amount must be bounded and miter_limit must be in [1, 1000]");
    if (params.amount == 0.0f) return source;
    const auto contours = flatten_path_for_geometry(source);
    Clipper2Lib::PathsD closed = closed_paths(contours);
    const bool has_open_geometry = std::any_of(contours.begin(), contours.end(),
        [](const FlattenedContour& contour) {
            return !contour.closed && contour.points.size() >= 2;
        });
    if (closed.empty() && !has_open_geometry) return source;
    if (!closed.empty()) {
        // Normalize winding and overlaps first so nested contours are treated as
        // holes consistently by the polygon offsetter.
        closed = Clipper2Lib::Union(closed, clipper_fill_rule(source.fill_rule),
                                    kClipperPrecision);
        closed = Clipper2Lib::InflatePaths(closed, params.amount,
            Clipper2Lib::JoinType::Miter, Clipper2Lib::EndType::Polygon,
            params.miter_limit, kClipperPrecision, kPathCurveTolerance);
    }
    PathShape output = source;
    output.commands.clear();
    append_closed_paths(output.commands, closed);
    for (const auto& contour : contours)
        if (!contour.closed)
            append_offset_open_contour(output.commands, contour, params.amount,
                                       params.miter_limit);
    output.closed = false;
    return output;
}

PathShape boolean_union(const PathShape& source, const BooleanUnionParams& params) {
    if (params.paths.size() > 1024)
        throw std::invalid_argument("boolean-union accepts at most 1024 additional subpaths");
    if (source.commands.size() > kPathGeometryLimit)
        throw std::invalid_argument("boolean-union input exceeds the geometry limit");
    auto source_paths = closed_paths(flatten_path_for_geometry(source), true);
    Clipper2Lib::PathsD inputs;
    if (!source_paths.empty()) {
        source_paths = Clipper2Lib::Union(source_paths,
            clipper_fill_rule(source.fill_rule), kClipperPrecision);
        inputs.insert(inputs.end(), source_paths.begin(), source_paths.end());
    }
    std::size_t command_count = source.commands.size();
    for (const auto& subpath : params.paths) {
        if (subpath.empty() || subpath.front().type != PathCommandType::MoveTo)
            throw std::invalid_argument("each boolean-union contour must begin with move_to");
        if (subpath.size() > kPathGeometryLimit - command_count)
            throw std::invalid_argument("boolean-union input exceeds the geometry limit");
        command_count += subpath.size();
        PathShape input_shape;
        input_shape.commands = subpath;
        input_shape.fill_rule = source.fill_rule;
        auto shape_paths = closed_paths(flatten_path_for_geometry(input_shape), true);
        if (shape_paths.empty()) continue;
        shape_paths = Clipper2Lib::Union(shape_paths,
            clipper_fill_rule(input_shape.fill_rule), kClipperPrecision);
        inputs.insert(inputs.end(), shape_paths.begin(), shape_paths.end());
    }
    const auto united = inputs.empty() ? Clipper2Lib::PathsD{} :
        Clipper2Lib::Union(inputs, Clipper2Lib::FillRule::NonZero, kClipperPrecision);
    PathShape output = source;
    output.commands.clear();
    append_closed_paths(output.commands, united);
    output.closed = false;
    output.fill_rule = PathFillRule::EvenOdd;
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
void append_polyline(std::vector<PathCommand>& out, const std::vector<Vec2>& points) {
    if (points.size() < 2) return;
    out.push_back(PathCommand::move_to(points.front()));
    for (std::size_t i = 1; i < points.size(); ++i)
        if (points[i] != points[i - 1]) out.push_back(PathCommand::line_to(points[i]));
}
std::optional<std::string> validate_morph_path_topology_impl(
    const std::vector<PathCommand>& source,
    const std::vector<PathCommand>& target) {
    if (source.empty() || source.size() != target.size())
        return "path-morph paths must have equal non-empty command sequences";
    bool source_open = false;
    bool target_open = false;
    for (std::size_t index = 0; index < source.size(); ++index) {
        const auto& from = source[index];
        const auto& to = target[index];
        if (from.type != to.type)
            return "path-morph paths must have identical command topology";
        const auto finite = [](const Vec2& point) {
            return std::isfinite(point.x) && std::isfinite(point.y);
        };
        if (from.type == PathCommandType::MoveTo) {
            // MoveTo also delimits open stroke contours; equal command
            // topology preserves that correspondence without requiring Close.
            source_open = target_open = true;
            if (!finite(from.p0) || !finite(to.p0))
                return "path-morph coordinates must be finite";
        } else if (from.type == PathCommandType::Close) {
            if (!source_open || !target_open)
                return "path-morph close command has no active contour";
            source_open = target_open = false;
        } else {
            if (!source_open || !target_open)
                return "path-morph drawing command has no active contour";
            if (!finite(from.p0) || !finite(to.p0))
                return "path-morph coordinates must be finite";
            if ((from.type == PathCommandType::QuadraticTo ||
                 from.type == PathCommandType::CubicTo) &&
                (!finite(from.p1) || !finite(to.p1)))
                return "path-morph coordinates must be finite";
            if (from.type == PathCommandType::CubicTo &&
                (!finite(from.p2) || !finite(to.p2)))
                return "path-morph coordinates must be finite";
        }
    }        return std::nullopt;
}
PathShape morph_path(const PathShape& source, const MorphPathParams& params) {
    if (!std::isfinite(params.progress) || params.progress < 0.0f ||
        params.progress > 1.0f)
        throw std::invalid_argument("path-morph progress must be finite and in [0, 1]");
    if (const auto error = validate_morph_path_topology_impl(source.commands, params.target))
        throw std::invalid_argument(*error);

    PathShape output = source;
    const auto interpolate = [progress = static_cast<double>(params.progress)](Vec2 from, Vec2 to) {
        // A convex interpolation of finite float endpoints is finite, but the
        // intermediate `to - from` in glm::mix can overflow for opposite-sign
        // large coordinates. Compute in double so extreme yet valid input
        // cannot manufacture infinities in the lowered path.
        const auto component = [progress](float a, float b) {
            return static_cast<float>(static_cast<double>(a) +
                (static_cast<double>(b) - static_cast<double>(a)) * progress);
        };
        return Vec2{component(from.x, to.x), component(from.y, to.y)};
    };
    for (std::size_t index = 0; index < output.commands.size(); ++index) {
        auto& command = output.commands[index];
        const auto& target = params.target[index];
        if (command.type == PathCommandType::Close) continue;
        command.p0 = interpolate(command.p0, target.p0);
        if (command.type == PathCommandType::QuadraticTo ||
            command.type == PathCommandType::CubicTo)
            command.p1 = interpolate(command.p1, target.p1);
        if (command.type == PathCommandType::CubicTo)
            command.p2 = interpolate(command.p2, target.p2);
    }
    return output;
}
PathShape trim_path(const PathShape& source, const TrimPathParams& params) {
    if (!std::isfinite(params.start) || !std::isfinite(params.end) ||
        !std::isfinite(params.offset) || params.start < 0.0f || params.end > 1.0f ||
        params.end < params.start || std::abs(params.offset) > 1000000.0f)
        throw std::invalid_argument("trim-path start, end, and offset must be finite and valid");
    if (params.start == 0.0f && params.end == 1.0f) return source;
    const float span = params.end - params.start;
    float start = params.start + params.offset;
    start -= std::floor(start); // wrap into [0, 1)
    const float end = start + span;
    PathShape output = source;
    output.commands.clear();
    output.closed = false;
    for (const auto& subpath : math::flatten_path(source, 24)) {
        std::vector<Vec2> visible = math::trim_polyline(subpath, start, end);
        if (end > 1.0f) {
            // The window wraps past the end of the path: the visible part is
            // [start, 1] plus the wrapped head [0, end - 1] as its own run so no
            // artificial connecting segment is drawn between them.
            append_polyline(output.commands, visible);
            visible = math::trim_polyline(subpath, 0.0f, end - 1.0f);
        }
        append_polyline(output.commands, visible);
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
void apply_repeater(PathShape& path, const RepeaterParams& params, std::uint32_t copy_index) {
    for (auto& command : path.commands) {
        if (command.type == PathCommandType::Close) continue;
        transform_point(command.p0, params, copy_index);
        if (command.type == PathCommandType::CubicTo || command.type == PathCommandType::QuadraticTo)
            transform_point(command.p1, params, copy_index);
        if (command.type == PathCommandType::CubicTo) transform_point(command.p2, params, copy_index);
    }
    multiply_opacity(path, std::pow(params.opacity_decay, static_cast<float>(copy_index)));
}


} // namespace chronon3d::path_operator_detail
