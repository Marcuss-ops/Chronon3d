#pragma once

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
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

struct Shape {
    ShapeType type{ShapeType::None};
    RectShape rect;
    RoundedRectShape rounded_rect;
    CircleShape circle;
    LineShape line;
    TextShape text;
    ImageShape image;
};

} // namespace chronon3d
