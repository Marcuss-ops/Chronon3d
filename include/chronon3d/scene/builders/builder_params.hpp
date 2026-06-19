#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <memory>
#include <optional>
#include <string>

namespace chronon3d {

// ── Forward declarations for TextRunParams (full headers included by users) ──
struct TextAnimatorSpec;
struct GlyphSelectorSpec;

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

// ═══════════════════════════════════════════════════════════════════════════
// Text composition structs — split the old 30-field TextParams monolith into
// four composable sub-structs (FontSpec already lives in font_engine.hpp).
//
//   TextSpec = TextContent + FontSpec + TextLayoutSpec + TextAppearanceSpec + position
//
// Callers can now:
//   - Build reusable font/layout/appearance presets
//   - Modify only one section without traversing 30 fields
//   - Pass shared sub-structs across compositions
// ═══════════════════════════════════════════════════════════════════════════

struct TextLayoutSpec {
    Vec2          box{900.0f, 160.0f};
    TextAnchor    anchor{TextAnchor::Center};
    TextCenteringMode centering_mode{TextCenteringMode::LayoutBox};
    TextAlign     align{TextAlign::Center};
    VerticalAlign vertical_align{VerticalAlign::Middle};
    TextWrap      wrap{TextWrap::Word};
    TextOverflow  overflow{TextOverflow::Clip};
    f32           line_height{1.2f};
    f32           tracking{0.0f};
    bool          auto_fit{false};
    f32           min_font_size{12.0f};
    f32           max_font_size{160.0f};
    int           max_lines{0};
    bool          ellipsis{false};
};

struct TextAppearanceSpec {
    Color                   color{1.0f, 1.0f, 1.0f, 1.0f};
    TextPaint               paint{};
    std::vector<TextShadow> shadows{};
    TextMaterial            material{};
    TextBoxStyle            box_style{};
};

struct TextContent {
    std::string                      value;
    std::shared_ptr<PlacedGlyphRun>  pre_shaped;
};

/// TextSpec — composable text descriptor (replaces flat TextParams).
///
/// Usage with designated initializers:
///   TextSpec ts{
///     .content    = {.value = "Hello"},
///     .font       = {.font_path = "...", .font_family = "Poppins",
///                    .font_weight = 700, .font_size = 72.0f},
///     .layout     = {.box = {900, 160}},
///     .appearance = {.color = Color::white()},
///   };
///
/// Usage with presets:
///   const FontSpec kHeroFont{.font_path="...", .font_family="Poppins",
///                             .font_weight=700, .font_size=96.0f};
///   TextSpec title{.content={.value="CHRONON"}, .font=kHeroFont};
struct TextSpec {
    TextContent        content;
    FontSpec           font;
    TextLayoutSpec     layout;
    TextAppearanceSpec appearance;
    Vec3               position{};
};

// ── TextParams: deprecated type-alias for TextSpec ─────────────────────
//
// The 30-field TextParams monolith has been retired in favour of the
// composable TextSpec (TextContent + FontSpec + TextLayoutSpec +
// TextAppearanceSpec + position).  `TextParams` is kept as a deprecated
// alias so any external code that still references the name continues to
// compile.  To migrate:
//   1. Construct via TextSpec{...} nested designated initializers, or
//   2. Read/write through TextSpec's sub-structs (.content.value, .font.*,
//      .layout.*, .appearance.*, .position).
// Internally the project uses TextSpec directly at all call sites; the
// alias exists only as a transition aid for external integrations.
//
// Note: because the two names are now identical, any field-set pattern
// like `TextParams tp; tp.text = "x";` will NOT compile — `TextSpec` has
// no `.text` field (use `.content.value`).  The deprecation attribute
// surfaces this at the type level so callers see migration guidance.
// TextParams kept as type alias for backward compatibility.
// TODO: remove after all external callers migrate to TextSpec.
using TextParams = TextSpec;

// ═══════════════════════════════════════════════════════════════════════════
// TextRunSpec — composable text-run descriptor (PR 4 canonical form).
//
// Composes TextSpec for all text rendering + adds animators / script / cache.
// ═══════════════════════════════════════════════════════════════════════════
struct TextRunSpec {
    TextSpec                       text;
    TextDirection                  direction{TextDirection::Auto};
    std::string                    language;              // BCP-47 tag (empty = auto)
    std::vector<TextAnimatorSpec>  animators;
    std::vector<GlyphSelectorSpec> selectors;  // top-level convenience selectors
    bool                           cache_layout{true};
};

// -----------------------------------------------------------------------
// TextRunParams — deprecated type-alias of TextRunSpec.
//
// The former flat `struct TextRunParams` (with separate `pos` / `color` /
// `font_path` / ... fields) has been removed.  Production code now reads
// and writes the composable nested fields:
//   spec.text.content.value    (was spec.text)
//   spec.text.font.font_path   (was spec.font_path)
//   spec.text.font.font_size   (was spec.font_size)
//   spec.text.font.font_weight (was spec.font_weight)
//   spec.text.appearance.color (was spec.color)
//   spec.text.position         (was spec.pos)
//   spec.text.layout.box       (was spec.size)
//   spec.text.layout.tracking  (was spec.tracking)
//   spec.text.layout.{anchor|align|vertical_align|wrap|line_height}
//   spec.text.appearance.{paint|shadows|material}
//   spec.text.content.pre_shaped (was spec.pre_shaped)
//   spec.{direction|language|animators|selectors|cache_layout} (top-level)
//
// The alias exists to keep external integrations that still reference the
// legacy name compiling during migration; it carries the SAME identity as
// TextRunSpec — enforced by a static_assert in
// tests/text/test_text_run_builder.cpp.
// ---------------------------------------------------------------------------
using TextRunParams = TextRunSpec;

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
