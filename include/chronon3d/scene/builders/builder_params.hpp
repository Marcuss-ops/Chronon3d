#pragma once

#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/shape.hpp>
#include <string>

namespace chronon3d {

struct RectParams {
    Vec2 size{100, 100};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
};

struct RoundedRectParams {
    Vec2 size{100, 100};
    f32 radius{8.0f};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
};

struct CircleParams {
    f32 radius{50.0f};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
};

struct LineParams {
    Vec3 from{0, 0, 0};
    Vec3 to{100, 0, 0};
    f32 thickness{1.0f};
    Color color{1, 1, 1, 1};
};

struct TextParams {
    std::string content;
    TextStyle   style;
    Vec3        pos{0, 0, 0};
    TextBox     box{};
};

struct ImageParams {
    std::string path;
    Vec2 size{100.0f, 100.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    f32 opacity{1.0f};
};

struct ContactShadowParams {
    Vec3  pos{0, 0, 0};       // position on the floor
    Vec2  size{300, 80};       // shadow footprint (width, depth)
    f32   blur{30.0f};         // blur radius in screen pixels
    f32   opacity{0.45f};      // overall shadow opacity
    Color color{0, 0, 0, 1};   // shadow tint
};

struct FakeExtrudedTextParams {
    std::string text;
    std::string font_path{"assets/fonts/Inter-Bold.ttf"};
    Vec3  pos{0, 0, 0};
    f32   font_size{80.0f};
    int   depth{32};
    Vec2  extrude_dir{0.8f, 1.0f};  // world-space X/Y per step
    f32   extrude_z_step{1.2f};
    Color front_color{0.96f, 0.94f, 0.88f, 1.0f};
    Color side_color{0.55f, 0.50f, 0.43f, 0.85f};
    f32   side_fade{0.25f};
    TextAlign align{TextAlign::Center};
    f32   highlight_opacity{0.09f};
    f32   bevel_size{1.5f};
    Vec3  light_dir{-0.577f, 0.577f, -0.577f};  // world-space, normalized
};

struct FakeBox3DParams {
    Vec3  pos{0, 0, 0};
    Vec2  size{200, 200};
    f32   depth{60};
    Color color{1, 1, 1, 1};
    f32   top_tint{0.15f};
    f32   side_tint{0.20f};
    bool  contact_shadow{false};
    bool  reflective{false};
    f32   floor_y{-210.0f};
};

struct GridPlaneParams {
    Vec3      pos{0, 0, 0};
    PlaneAxis axis{PlaneAxis::XZ};
    f32       extent{2000};
    f32       spacing{200};
    Color     color{1, 1, 1, 0.25f};
    f32       fade_distance{1800.0f};  // 0 = no fade
    f32       fade_min_alpha{0.0f};    // alpha floor at max distance
};

} // namespace chronon3d
