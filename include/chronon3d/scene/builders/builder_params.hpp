#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <memory>
#include <optional>
#include <string>

namespace chronon3d {

struct RectParams {
    Vec2 size{100, 100};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
    std::optional<graphics::FillStyle> fill{};
    graphics::StrokeStyle stroke{};
};

struct RoundedRectParams {
    Vec2 size{100, 100};
    f32 radius{8.0f};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
    std::optional<graphics::FillStyle> fill{};
    graphics::StrokeStyle stroke{};
};

struct CircleParams {
    f32 radius{50.0f};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
    std::optional<graphics::FillStyle> fill{};
    graphics::StrokeStyle stroke{};
};

struct ShapeStrokeParams {
    f32 trim_start{0.0f};  // normalised [0..1]
    f32 trim_end{1.0f};
    bool enabled{true};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 width{1.0f};
    StrokeAlignment alignment{StrokeAlignment::Center};
};

struct LineParams {
    Vec3 from{0, 0, 0};
    Vec3 to{100, 0, 0};
    f32 thickness{1.0f};
    Color color{1, 1, 1, 1};
    ShapeStrokeParams stroke{};
};

struct PathParams {
    std::vector<PathCommand> commands;
    PathStroke stroke{};
    Fill fill{};
    Color color{1, 1, 1, 1};
    Vec3 pos{0, 0, 0};
    bool closed{false};
};

struct ImageParams {
    std::string path;
    Vec2 size{100.0f, 100.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    FitMode fit{FitMode::Cover};
    Vec2 focal_point{0.5f, 0.5f};
    ImageCrop crop{};
    f32 opacity{1.0f};
    f32 radius{0.0f};
};

struct GridBackgroundParams {
    Vec2 size{1920.0f, 1080.0f};
    Vec2 offset{0.0f, 0.0f};
    Color bg_color{0.008f, 0.010f, 0.022f, 1.0f};
    Color grid_color{0.25f, 0.52f, 1.0f, 0.05f};
    f32 spacing{140.0f};
    f32 minor_thickness{1.25f};
    f32 major_thickness{2.75f};
    i32 major_every{4};
    bool centered{true};
};

struct TextParams {
    std::string text;
    Vec2 size{900.0f, 160.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    std::string font_path{"assets/fonts/Inter-Bold.ttf"};
    std::string font_family{"Inter"};
    int font_weight{800};
    std::string font_style{"normal"};
    f32 font_size{72.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    // ── TextAnchor — how the box is anchored to pos ──────────────
    TextAnchor anchor{TextAnchor::Center};

    // ── Centering mode (LayoutBox = default, PixelInk = opt-in) ────
    TextCenteringMode centering_mode{TextCenteringMode::LayoutBox};

    TextAlign align{TextAlign::Center};
    VerticalAlign vertical_align{VerticalAlign::Middle};
    f32 line_height{1.2f};
    f32 tracking{0.0f};
    TextBoxStyle box_style{};

    TextPaint paint{};
    std::vector<TextShadow> shadows{};
    TextMaterial material{};

    bool auto_fit{false};
    int max_lines{0};
    bool ellipsis{false};
    f32 min_font_size{12.0f};
    f32 max_font_size{160.0f};
    TextOverflow overflow{TextOverflow::Clip};
    TextWrap wrap{TextWrap::Word};

    // ── Optional pre-shaped glyph run ───────────────────────────────
    // When set, the rasterizer uses these pre-shaped glyphs directly
    // instead of re-shaping the text with HarfBuzz.  Used by
    // typewriter_build() to preserve contextual Arabic/Indic shaping.
    std::shared_ptr<PlacedGlyphRun> pre_shaped;
};

struct ShadowStyle {
    TextShadow contact{
        .enabled = true,
        .offset = {0.0f, 6.0f},
        .blur = 14.0f,
        .opacity = 0.28f,
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    };
    TextShadow ambient{
        .enabled = true,
        .offset = {0.0f, 40.0f},
        .blur = 120.0f,
        .opacity = 0.08f,
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    };
};

struct ContactShadowParams {
    Vec3  pos{0, 0, 0};       // position on the floor
    Vec2  size{300, 80};       // shadow footprint (width, depth)
    f32   blur{30.0f};         // blur radius in screen pixels
    f32   opacity{0.45f};      // overall shadow opacity
    Color color{0, 0, 0, 1};   // shadow tint
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

struct DarkGridBgParams {
    Color bg_color   {0.098f, 0.098f, 0.11f, 1.f};
    Color grid_color {1.f,    1.f,    1.f,   0.14f};
    f32   spacing    {80.f};
    f32   extent     {4000.f};
    bool  centered   {false};
};

} // namespace chronon3d
