#pragma once

#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {

enum class PathCommandType { MoveTo, LineTo, CubicTo, QuadraticTo, Close };

/** A single Bezier path command. */
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

} // namespace chronon3d
