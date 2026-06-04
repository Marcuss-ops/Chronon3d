#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/fill.hpp>
#include <vector>

namespace chronon3d {

enum class PathCommandType {
    MoveTo,
    LineTo,
    CubicTo,
    QuadraticTo,
    Close
};

/**
 * A single command in a Bezier path.
 * p0: Used by MoveTo, LineTo, CubicTo, QuadraticTo
 * p1: Used by CubicTo, QuadraticTo (control point 1)
 * p2: Used by CubicTo (control point 2)
 */
struct PathCommand {
    PathCommandType type{PathCommandType::MoveTo};
    Vec2 p0{0, 0};
    Vec2 p1{0, 0};
    Vec2 p2{0, 0};

    static PathCommand move_to(Vec2 p) { return {PathCommandType::MoveTo, p}; }
    static PathCommand line_to(Vec2 p) { return {PathCommandType::LineTo, p}; }
    static PathCommand quadratic_to(Vec2 cp, Vec2 p) { 
        return {PathCommandType::QuadraticTo, p, cp}; 
    }
    static PathCommand cubic_to(Vec2 cp1, Vec2 cp2, Vec2 p) { 
        return {PathCommandType::CubicTo, p, cp1, cp2}; 
    }
    static PathCommand close() { return {PathCommandType::Close}; }
};

enum class LineCap {
    Butt,
    Round,
    Square
};

enum class LineJoin {
    Miter,
    Round,
    Bevel
};

struct PathStroke {
    bool enabled{true};
    Color color{1, 1, 1, 1};
    f32 width{1.0f};
    LineCap cap{LineCap::Butt};
    LineJoin join{LineJoin::Miter};
    std::vector<f32> dash_array{};
    f32 dash_offset{0.0f};
    f32 trim_start{0.0f};
    f32 trim_end{1.0f};
};

struct PathShape {
    std::vector<PathCommand> commands;
    PathStroke stroke{};
    Fill fill{};
    bool closed{false};
};

} // namespace chronon3d
