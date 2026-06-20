#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <chronon3d/text/font_engine.hpp>  // TextDirection, TextShaping, TextWrap

namespace chronon3d {

// Forward declarations for types owned by other headers.
// Full definitions live in:
//   TextRunShape  → <chronon3d/text/text_run.hpp>
//   Mesh          → <chronon3d/geometry/mesh.hpp>
struct TextRunShape;
struct Mesh;

enum class ShapeType {
    None,
    Rect,
    RoundedRect,
    Circle,
    Line,
    Path,
    Text,
    Image,
    TiledImage,       // dedicated TiledImageShape payload (index 12)
    GridBackground,
    Mesh,             // dedicated MeshShape payload (index 13)
    FakeBox3D,
    GridPlane,
    FakeExtrudedText,
    TextRun,          // now has dedicated TextRunShapeHandle payload
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

    /// Optional gradient fill.  When present, the stroke colours come
    /// from the gradient instead of the solid `color` field.
    std::optional<GradientFill> gradient;
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

    /// Optional gradient/colour override for stroke.  When present, the
    /// stroke uses this Fill instead of `stroke_color`, enabling gradient
    /// strokes for text (Linear, Radial, Conic) just like shape strokes.
    /// Falls back to solid `stroke_color` when absent.
    std::optional<Fill> stroke_style{};
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

    // ── Optional pre-shaped glyph run ───────────────────────────────
    // When set, the rasterizer SKIPS HarfBuzz shaping and uses these
    // glyphs directly.  This is critical for typewriter-style layers
    // where each character is a separate layer: without pre-shaping,
    // a character rendered in isolation would lose its contextual form
    // (e.g. Arabic medial → isolated).
    //
    // The PlacedGlyphRun must contain glyphs with resolved positions.
    // The rasterizer still requires the font to be loaded for
    // Blend2D glyph access (fillGlyphRun / strokePath) — only the
    // HarfBuzz shaping step is bypassed.
    std::shared_ptr<PlacedGlyphRun> pre_shaped;
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

// ── New distinct variant payloads (PR: fix ambiguous ShapeType → index mapping) ─

/// Tiled-image payload — wraps an ImageShape so TiledImage has its own variant
/// index instead of aliasing ImageShape (index 7).
struct TiledImageShape {
    ImageShape image;
};

/// Mesh payload — carries a shared_ptr<Mesh> so Mesh has its own variant
/// index instead of aliasing std::monostate (index 0).
struct MeshShape {
    std::shared_ptr<Mesh> mesh;
};

/// TextRun payload — carries a shared_ptr<TextRunShape> so TextRun has its
/// own variant index instead of aliasing std::monostate (index 0) + the
/// parallel `is_text_run_shape` flag on RenderNode.
struct TextRunShapeHandle {
    std::shared_ptr<TextRunShape> value;
};

// ── ShapePayload variant ───────────────────────────────────────────────────
// Replaces the old "all payloads together" layout with a discriminated union.
// Variant index → ShapeType mapping (one-to-one, no aliasing):
//   0 = None (std::monostate)
//   1 = Rect
//   2 = RoundedRect
//   3 = Circle
//   4 = Line
//   5 = Path
//   6 = Text
//   7 = Image
//   8 = GridBackground
//   9 = FakeBox3D
//  10 = GridPlane
//  11 = FakeExtrudedText
//  12 = TiledImage
//  13 = Mesh
//  14 = TextRun

using ShapePayload = std::variant<
    std::monostate,          //  0: None
    RectShape,               //  1: Rect
    RoundedRectShape,        //  2: RoundedRect
    CircleShape,             //  3: Circle
    LineShape,               //  4: Line
    PathShape,               //  5: Path
    TextShape,               //  6: Text
    ImageShape,              //  7: Image
    GridBackgroundShape,     //  8: GridBackground
    FakeBox3DShape,          //  9: FakeBox3D
    GridPlaneShape,          // 10: GridPlane
    FakeExtrudedTextShape,   // 11: FakeExtrudedText
    TiledImageShape,         // 12: TiledImage
    MeshShape,               // 13: Mesh
    TextRunShapeHandle       // 14: TextRun
>;

// ── ShapeType ↔ variant index conversion ────────────────────────────────────

inline ShapeType shape_type_from_payload(const ShapePayload& p) {
    switch (p.index()) {
        case 0:  return ShapeType::None;
        case 1:  return ShapeType::Rect;
        case 2:  return ShapeType::RoundedRect;
        case 3:  return ShapeType::Circle;
        case 4:  return ShapeType::Line;
        case 5:  return ShapeType::Path;
        case 6:  return ShapeType::Text;
        case 7:  return ShapeType::Image;
        case 8:  return ShapeType::GridBackground;
        case 9:  return ShapeType::FakeBox3D;
        case 10: return ShapeType::GridPlane;
        case 11: return ShapeType::FakeExtrudedText;
        case 12: return ShapeType::TiledImage;
        case 13: return ShapeType::Mesh;
        case 14: return ShapeType::TextRun;
        default: return ShapeType::None;
    }
}

inline size_t payload_index_for_type(ShapeType t) {
    switch (t) {
        case ShapeType::None:             return 0;
        case ShapeType::Rect:             return 1;
        case ShapeType::RoundedRect:      return 2;
        case ShapeType::Circle:           return 3;
        case ShapeType::Line:             return 4;
        case ShapeType::Path:             return 5;
        case ShapeType::Text:             return 6;
        case ShapeType::Image:            return 7;
        case ShapeType::TiledImage:       return 12;
        case ShapeType::GridBackground:   return 8;
        case ShapeType::FakeBox3D:        return 9;
        case ShapeType::GridPlane:        return 10;
        case ShapeType::FakeExtrudedText: return 11;
        case ShapeType::Mesh:             return 13;
        case ShapeType::TextRun:          return 14;
        default:                          return 0;
    }
}

// ── Shape ──────────────────────────────────────────────────────────────────

struct Shape {
    using Payload = ShapePayload;

    Payload payload;

    Shape() = default;

    /// Construct with a specific payload type.
    template <typename T>
    explicit Shape(T&& val) : payload(std::forward<T>(val)) {}

    /// Derive the ShapeType from the variant index.
    [[nodiscard]] ShapeType type() const { return shape_type_from_payload(payload); }

    /// Set the type (discriminant) and default-construct the matching payload.
    void set_type(ShapeType t) {
        switch (payload_index_for_type(t)) {
            case 0:  payload.emplace<0>();  break;
            case 1:  payload.emplace<1>();  break;
            case 2:  payload.emplace<2>();  break;
            case 3:  payload.emplace<3>();  break;
            case 4:  payload.emplace<4>();  break;
            case 5:  payload.emplace<5>();  break;
            case 6:  payload.emplace<6>();  break;
            case 7:  payload.emplace<7>();  break;
            case 8:  payload.emplace<8>();  break;
            case 9:  payload.emplace<9>();  break;
            case 10: payload.emplace<10>(); break;
            case 11: payload.emplace<11>(); break;
            case 12: payload.emplace<12>(); break;
            case 13: payload.emplace<13>(); break;
            case 14: payload.emplace<14>(); break;
            default: payload.emplace<0>();  break;
        }
    }

    // ── Named accessors ─────────────────────────────────────────────────
    // Each returns a reference to the variant member, asserting the correct
    // type via std::get.  Use these instead of the old shape.rect / shape.text
    // fields.  They behave exactly like the old fields but with an added
    // debug-mode assertion that the shape type matches.

    [[nodiscard]] RectShape&               rect()                { return std::get<1>(payload); }
    [[nodiscard]] const RectShape&         rect()          const { return std::get<1>(payload); }
    [[nodiscard]] RoundedRectShape&        rounded_rect()        { return std::get<2>(payload); }
    [[nodiscard]] const RoundedRectShape&  rounded_rect()  const { return std::get<2>(payload); }
    [[nodiscard]] CircleShape&             circle()              { return std::get<3>(payload); }
    [[nodiscard]] const CircleShape&       circle()        const { return std::get<3>(payload); }
    [[nodiscard]] LineShape&               line()                { return std::get<4>(payload); }
    [[nodiscard]] const LineShape&         line()          const { return std::get<4>(payload); }
    [[nodiscard]] PathShape&               path()                { return std::get<5>(payload); }
    [[nodiscard]] const PathShape&         path()          const { return std::get<5>(payload); }
    [[nodiscard]] TextShape&               text()                { return std::get<6>(payload); }
    [[nodiscard]] const TextShape&         text()          const { return std::get<6>(payload); }
    [[nodiscard]] ImageShape&              image()               { return std::get<7>(payload); }
    [[nodiscard]] const ImageShape&        image()         const { return std::get<7>(payload); }
    [[nodiscard]] GridBackgroundShape&     grid_background()     { return std::get<8>(payload); }
    [[nodiscard]] const GridBackgroundShape& grid_background() const { return std::get<8>(payload); }
    [[nodiscard]] FakeBox3DShape&          fake_box3d()          { return std::get<9>(payload); }
    [[nodiscard]] const FakeBox3DShape&    fake_box3d()    const { return std::get<9>(payload); }
    [[nodiscard]] GridPlaneShape&          grid_plane()          { return std::get<10>(payload); }
    [[nodiscard]] const GridPlaneShape&    grid_plane()    const { return std::get<10>(payload); }
    [[nodiscard]] FakeExtrudedTextShape&   fake_extruded_text()  { return std::get<11>(payload); }
    [[nodiscard]] const FakeExtrudedTextShape& fake_extruded_text() const { return std::get<11>(payload); }
    [[nodiscard]] TiledImageShape&         tiled_image()         { return std::get<12>(payload); }
    [[nodiscard]] const TiledImageShape&   tiled_image()   const { return std::get<12>(payload); }
    [[nodiscard]] MeshShape&               mesh_shape()          { return std::get<13>(payload); }
    [[nodiscard]] const MeshShape&         mesh_shape()    const { return std::get<13>(payload); }
    [[nodiscard]] TextRunShapeHandle&      text_run_shape_handle()       { return std::get<14>(payload); }
    [[nodiscard]] const TextRunShapeHandle& text_run_shape_handle() const { return std::get<14>(payload); }
};

} // namespace chronon3d
