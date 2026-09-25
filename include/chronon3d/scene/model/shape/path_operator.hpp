#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/path_commands.hpp>
#include <cstdint>
#include <variant>
#include <vector>

namespace chronon3d {
struct PathShape;

enum class PathOperatorKind : std::uint8_t { Repeater, RoundCorners, ZigZag, OffsetPath, PuckerBloat, Twist, WigglePath, Merge, Trim };
struct RepeaterParams {
    std::uint32_t copies{1};
    Vec2 position{0.0f};
    Vec2 scale{1.0f};
    float rotation_degrees{0.0f};
    Vec2 anchor{0.0f};
    float opacity_decay{1.0f};
};
struct RoundCornersParams { float radius{0.0f}; };
struct ZigZagParams { float size{0.0f}; float points_per_unit{0.1f}; bool smooth{false}; };
struct OffsetPathParams { float amount{0.0f}; float miter_limit{4.0f}; };
struct PuckerBloatParams { float amount{0.0f}; Vec2 anchor{0.0f}; };
struct TwistParams { float degrees{0.0f}; Vec2 center{0.0f}; };
struct WigglePathParams { float amount{0.0f}; float frequency{1.0f}; };
struct MergeParams { std::vector<std::vector<PathCommand>> paths; };
// Trim Paths: keeps the arc-length window [start, end] of each subpath
// (fractions of that subpath's length) shifted by `offset`. The window wraps
// around the path end, so an offset can carry the visible segment through the
// start point. start == 0 && end == 1 is the identity regardless of offset.
struct TrimPathParams { float start{0.0f}; float end{1.0f}; float offset{0.0f}; };
using PathOperatorParams = std::variant<RepeaterParams, RoundCornersParams, ZigZagParams, OffsetPathParams, PuckerBloatParams, TwistParams, WigglePathParams, MergeParams, TrimPathParams>;
struct PathOperatorSpec {
    PathOperatorKind kind{PathOperatorKind::Repeater};
    PathOperatorParams params{RepeaterParams{}};
    std::uint32_t seed{0};
};

// Operators are evaluated during lowering. Repeater returns independent shapes so
// each copy can carry its own opacity; other operators preserve one shape.
std::vector<PathShape> apply_path_operators(const PathShape& input,
    const std::vector<PathOperatorSpec>& operators);
} // namespace chronon3d
