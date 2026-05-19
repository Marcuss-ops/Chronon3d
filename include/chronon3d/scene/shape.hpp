#pragma once

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/scene/fill.hpp>
#include <chronon3d/scene/path.hpp>
#include <string>

namespace chronon3d {

enum class ShapeType {
    None,
    Rect,
    RoundedRect,
    Circle,
    Line,
    Path,
    Image,
    Mesh,
    FakeBox3D,
    GridPlane,
};

enum class PlaneAxis { XZ, XY };

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

struct LineStroke {
    f32 trim_start{0.0f};  // normalised [0..1]
    f32 trim_end{1.0f};
};

struct LineShape {
    Vec3 to{0.0f, 0.0f, 0.0f};
    f32 thickness{1.0f};
    LineStroke stroke{};
};



struct ImageShape {
    std::string path;
    Vec2 size{100.0f, 100.0f};
    f32 opacity{1.0f};
};

struct FakeBox3DShape {
    Vec3  world_pos{0, 0, 0};   // local-space center (transformed by world_matrix)
    Vec2  size{200, 200};        // width (X), height (Y) in local units
    f32   depth{60};             // Z extrusion depth
    Color color{1, 1, 1, 1};
    f32   top_tint{0.15f};      // brighten top face by this amount
    f32   side_tint{0.20f};     // darken side faces by this amount
};

struct GridPlaneShape {
    Vec3      world_pos{0, 0, 0};
    PlaneAxis axis{PlaneAxis::XZ};
    f32       extent{2000};
    f32       spacing{200};
    Color     color{1, 1, 1, 0.25f};
    f32       fade_distance{1800.0f};
    f32       fade_min_alpha{0.0f};
};



struct Shape {
    ShapeType type{ShapeType::None};
    RectShape rect;
    RoundedRectShape rounded_rect;
    CircleShape circle;
    LineShape line;
    PathShape path;
    ImageShape image;
    FakeBox3DShape fake_box3d;
    GridPlaneShape grid_plane;
};

} // namespace chronon3d
