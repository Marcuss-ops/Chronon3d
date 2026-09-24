#include <chronon3d/scene/model/shape/path.hpp>
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
}
std::vector<PathShape> apply_path_operators(const PathShape& input,
    const std::vector<PathOperatorSpec>& operators) {
    std::vector<PathShape> paths{input};
    for (const auto& op : operators) {
        if (op.kind != PathOperatorKind::Repeater || !std::holds_alternative<RepeaterParams>(op.params))
            throw std::invalid_argument("unsupported or mismatched path operator");
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
    }
    return paths;
}
} // namespace chronon3d
