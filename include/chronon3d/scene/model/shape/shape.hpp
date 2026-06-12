#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <optional>
#include <string>
#include <vector>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <chronon3d/text/font_engine.hpp>  // TextDirection, TextShaping

namespace chronon3d {

enum class ShapeType {
    None,
    Rect,
    RoundedRect,
    Circle,
    Line,
    Path,
    Text,
    Image,
    TiledImage,
    GridBackground,
    Mesh,
    FakeBox3D,
    GridPlane,
    FakeExtrudedText,
};

enum class PlaneAxis { XZ, XY };

enum class StrokeAlignment {
    Center,
    Inside,
    Outside,
};

struct ShapeStroke {
    bool enabled{false};
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
    f32 width{1.0f};
    StrokeAlignment alignment{StrokeAlignment::Center};
};

struct RectShape {
    Vec2 size{100.0f, 100.0f};
    ShapeStroke stroke{};
};

// Rounded rectangle — corners follow a circular arc of the given radius.
// Radius is clamped to min(width, height) / 2 at render time.
struct RoundedRectShape {
    Vec2 size{100.0f, 100.0f};
    f32 radius{8.0f};
    ShapeStroke stroke{};
};

struct CircleShape {
    f32 radius{50.0f};
    ShapeStroke stroke{};
};

struct LineStroke {
    f32 trim_start{0.0f};  // normalised [0..1]
    f32 trim_end{1.0f};
    bool enabled{true};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 width{1.0f};
    StrokeAlignment alignment{StrokeAlignment::Center};
};

struct LineShape {
    Vec3 to{0.0f, 0.0f, 0.0f};
    f32 thickness{1.0f};
    LineStroke stroke{};
};

enum class TextAlign { Left, Center, Right };
enum class VerticalAlign { Top, Middle, Bottom };

// ── TextAnchor — defines how the text box is anchored to pos ─────
// Determines which point of the text box corresponds to TextParams.pos.
//
//   Center:       pos = center of the text box
//   TopLeft:      pos = top-left corner
//   TopCenter:    pos = top-center
//   BottomCenter: pos = bottom-center
//   BaselineLeft: pos = left end of the first baseline (approx)
//   BaselineCenter: pos = center of the first baseline (approx)
//
// This is SEPARATE from TextAlign/VerticalAlign, which control how
// text is positioned INSIDE the box.
enum class TextAnchor : u8 {
    Center,
    TopLeft,
    TopCenter,
    BottomCenter,
    BaselineLeft,
    BaselineCenter,
};

// ── TextCenteringMode — controls how centered text is positioned ───
// Determines the method used to compute the centering offset for text
// with TextAlign::Center.
//
//   LayoutBox:  center is computed from the layout engine's reported text
//               bounding box (default). Fast and consistent.
//   PixelInk:   center is computed by scanning the rasterized pixel ink.
//               More accurate when font measurement (layout) disagrees
//               with the actual rendered glyph shapes, but slower.
//
// LayoutBox is strongly recommended for all production use. PixelInk
// exists as a safety net for debugging / migration off legacy content.
enum class TextCenteringMode : u8 {
    LayoutBox,
    PixelInk,
};

struct TextPaint {
    Color fill{1.0f, 1.0f, 1.0f, 1.0f};
    std::optional<Fill> fill_style{};
    bool stroke_enabled{false};
    Color stroke_color{0.0f, 0.0f, 0.0f, 1.0f};
    f32 stroke_width{2.0f};
};

struct TextShadow {
    bool enabled{false};
    Vec2 offset{0.0f, 0.0f};
    f32 blur{0.0f};
    f32 opacity{1.0f};
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
};

struct TextBoxStyle {
    bool enabled{false};
    Vec2 padding{0.0f, 0.0f};
    f32 radius{0.0f};
    Color background{0.0f, 0.0f, 0.0f, 0.0f};
    bool border_enabled{false};
    Color border_color{0.0f, 0.0f, 0.0f, 0.0f};
    f32 border_width{1.0f};
};

enum class TextOverflow {
    Clip,
    Ellipsis,
};

enum class TextWrap {
    None,
    Word,
    Character,
};

// TextDirection and TextShaping are defined in <chronon3d/text/font_engine.hpp>.

struct TextStyle {
    std::string font_path;
    std::string font_family;
    int   font_weight{400};
    std::string font_style{"normal"};
    f32   size{32.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    // ── TextAnchor — how the box is anchored to pos ──────────────
    TextAnchor anchor{TextAnchor::Center};

    f32 line_height{1.2f};   // multiplier of font size
    f32 tracking{0.0f};      // extra px per glyph advance

    int  max_lines{0};       // 0 = unlimited
    bool auto_scale{false};  // shrink font to fit TextBox
    f32  min_size{12.0f};
    f32  max_size{256.0f};

    bool auto_fit{false};
    bool ellipsis{false};
    TextOverflow overflow{TextOverflow::Clip};
    TextWrap wrap{TextWrap::Word};

    // V2 Typography
    TextPaint paint{};
    std::vector<TextShadow> shadows{};
    TextBoxStyle box_style{};

    // ── Centering mode (LayoutBox = default, PixelInk = opt-in) ────
    TextCenteringMode centering_mode{TextCenteringMode::LayoutBox};

    // ── Intra-box alignment (separate from box anchoring) ─────────
    TextAlign align{TextAlign::Center};
    VerticalAlign vertical_align{VerticalAlign::Middle};

    // Premium TextMaterial (gradient, bevel, highlight, etc.)
    TextMaterial material{};

    // Shaping control (direction, script, language)
    // Added in 2026 — empty defaults auto-detect from text content.
    TextShaping shaping{};
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

struct ImageCrop {
    bool enabled{false};
    Vec2 origin{0.0f, 0.0f}; // normalized
    Vec2 size{1.0f, 1.0f};   // normalized
};

struct ImageShape {
    std::string path;
    Vec2 size{100.0f, 100.0f};
    FitMode fit{FitMode::Cover};
    Vec2 focal_point{0.5f, 0.5f};
    ImageCrop crop{};
    f32 opacity{1.0f};
    f32 radius{0.0f};
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

struct GridBackgroundShape {
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
    f32   bevel_size{1.5f};

    // Light direction for animated glint sweep (world-space, normalized).
    Vec3  light_dir{-0.577f, 0.577f, -0.577f};
};

struct Shape {
    ShapeType type{ShapeType::None};
    RectShape rect;
    RoundedRectShape rounded_rect;
    CircleShape circle;
    LineShape line;
    PathShape path;
    TextShape text;
    ImageShape image;
    GridBackgroundShape grid_background;
    FakeBox3DShape fake_box3d;
    GridPlaneShape grid_plane;
    FakeExtrudedTextShape fake_extruded_text;
};

} // namespace chronon3d
