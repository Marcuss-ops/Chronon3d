#pragma once

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/types.hpp>
#include <string>

namespace chronon3d {

enum class ShapeType {
    None,
    Rect,
    RoundedRect,
    Circle,
    Line,
    Text,
    Image,
    Mesh,
    FakeBox3D,
    GridPlane,
    FakeExtrudedText,
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

struct LineShape {
    Vec3 to{0.0f, 0.0f, 0.0f};
    f32 thickness{1.0f};
};

enum class TextAlign { Left, Center, Right };

struct TextStyle {
    std::string font_path;
    f32   size{32.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    TextAlign align{TextAlign::Left};

    f32 line_height{1.2f};   // multiplier of font size
    f32 tracking{0.0f};      // extra px per glyph advance

    int  max_lines{0};       // 0 = unlimited
    bool auto_scale{false};  // shrink font to fit TextBox
    f32  min_size{12.0f};
    f32  max_size{256.0f};
};

// Optional bounding box for word-wrap and auto-scale.
struct TextBox {
    Vec2 size{0.0f, 0.0f};
    bool enabled{false};
};

struct TextShape {
    std::string text;
    TextStyle   style{};
    TextBox     box{};
};

struct ImageShape {
    std::string path;
    Vec2 size{100.0f, 100.0f};
    f32 opacity{1.0f};
};

struct FakeBox3DShape {
    Vec3  world_pos{0, 0, 0};   // 3D world-space center (must match layer position)
    Vec2  size{200, 200};        // width (X), height (Y) in world units
    f32   depth{60};             // Z extrusion depth (positive = away from camera)
    Color color{1, 1, 1, 1};
    f32   top_tint{0.15f};      // brighten top face by this amount
    f32   side_tint{0.20f};     // darken side faces by this amount
    // Injected at render time by build_render_graph:
    bool  cam_ready{false};
    Mat4  cam_view{1.0f};
    f32   cam_focal{1000.0f};
    f32   vp_cx{640};
    f32   vp_cy{360};
};

struct GridPlaneShape {
    Vec3      world_pos{0, 0, 0};
    PlaneAxis axis{PlaneAxis::XZ};
    f32       extent{2000};      // half-size in each plane direction
    f32       spacing{200};      // line spacing
    Color     color{1, 1, 1, 0.25f};
    // Injected at render time:
    bool  cam_ready{false};
    Mat4  cam_view{1.0f};
    f32   cam_focal{1000.0f};
    f32   vp_cx{640};
    f32   vp_cy{360};
};

struct FakeExtrudedTextShape {
    std::string text;
    std::string font_path{"assets/fonts/Inter-Bold.ttf"};
    f32   font_size{80.0f};
    TextAlign align{TextAlign::Center};

    Vec3  world_pos{0, 0, 0};

    int   depth{32};
    Vec2  extrude_dir{0.8f, 1.0f};  // world-space X/Y offset per step
    f32   extrude_z_step{1.2f};      // world-space Z offset per step (positive = away)

    Color front_color{0.96f, 0.94f, 0.88f, 1.0f};
    Color side_color{0.55f, 0.50f, 0.43f, 0.85f};
    f32   side_fade{0.25f};          // how much to fade side alpha toward deepest step
    f32   highlight_opacity{0.09f};

    // Injected at render time by build_render_graph:
    bool  cam_ready{false};
    Mat4  cam_view{1.0f};
    f32   cam_focal{1000.0f};
    f32   vp_cx{640};
    f32   vp_cy{360};
};

struct Shape {
    ShapeType type{ShapeType::None};
    RectShape rect;
    RoundedRectShape rounded_rect;
    CircleShape circle;
    LineShape line;
    TextShape text;
    ImageShape image;
    FakeBox3DShape fake_box3d;
    GridPlaneShape grid_plane;
    FakeExtrudedTextShape fake_extruded_text;
};

} // namespace chronon3d
