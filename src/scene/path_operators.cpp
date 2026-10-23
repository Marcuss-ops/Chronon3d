#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/animation/effects/wiggle.hpp>
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace chronon3d {
namespace {
constexpr double kPathCurveTolerance = 0.05;
constexpr int kClipperPrecision = 4;
constexpr std::size_t kPathGeometryLimit = 1000000;

struct FlattenedContour {
    std::vector<Clipper2Lib::PointD> points;
    bool closed{false};
};

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
                                bool close_open_contours = false) {
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
}
std::optional<std::string> validate_morph_path_topology(
    const std::vector<PathCommand>& source,
    const std::vector<PathCommand>& target) {
    return validate_morph_path_topology_impl(source, target);
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
        } else if (op.kind == PathOperatorKind::Trim) {
            if (!std::holds_alternative<TrimPathParams>(op.params))
                throw std::invalid_argument("trim-path parameters do not match operator kind");
            for (auto& path : paths) path = trim_path(path, std::get<TrimPathParams>(op.params));
        } else if (op.kind == PathOperatorKind::Morph) {
            if (!std::holds_alternative<MorphPathParams>(op.params))
                throw std::invalid_argument("morph parameters do not match operator kind");
            for (auto& path : paths) path = morph_path(path, std::get<MorphPathParams>(op.params));
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
        } else if (op.kind == PathOperatorKind::BooleanUnion) {
            if (!std::holds_alternative<BooleanUnionParams>(op.params))
                throw std::invalid_argument("boolean-union parameters do not match operator kind");
            for (auto& path : paths)
                path = boolean_union(path, std::get<BooleanUnionParams>(op.params));
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
