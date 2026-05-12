#pragma once

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/core/types.hpp>

namespace chronon3d {

enum class ShapeType {
    None,
    Rect,
    RoundedRect,
    Circle,
    Line,
    Mesh
};

struct RectShape {
    Vec2 size{100.0f, 100.0f};
};

// Rounded rectangle — corners follow a circular arc of the given radius.
// Radius is clamped to min(width, height) / 2 at render time.
struct RoundedRectShape {
    Vec2 size{100.0f, 100.0f};
    f32 radius{8.0f};
};

struct CircleShape {
    f32 radius{50.0f};
};

struct LineShape {
    Vec3 to{0.0f, 0.0f, 0.0f};
    f32 thickness{1.0f};
};

struct Shape {
    ShapeType type{ShapeType::None};
    RectShape rect;
    RoundedRectShape rounded_rect;
    CircleShape circle;
    LineShape line;
};

} // namespace chronon3d
