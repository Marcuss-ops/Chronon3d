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

// ── Forward declarations for TextRunParams (full headers included by users) ─
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

// ── Backward-compatible TextParams (thin wrapper delegating to TextSpec) ─
// After all call sites migrate, TextParams will become a using alias.
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

    // ── Conversion from TextSpec (forward-compat bridge) ──────────
    TextParams() = default;
    /*implicit*/ TextParams(const TextSpec& ts)
        : text(ts.content.value)
        , size(ts.layout.box)
        , pos(ts.position)
        , font_path(ts.font.font_path)
        , font_family(ts.font.font_family)
        , font_weight(ts.font.font_weight)
        , font_style(ts.font.font_style)
        , font_size(ts.font.font_size)
        , color(ts.appearance.color)
        , anchor(ts.layout.anchor)
        , centering_mode(ts.layout.centering_mode)
        , align(ts.layout.align)
        , vertical_align(ts.layout.vertical_align)
        , line_height(ts.layout.line_height)
        , tracking(ts.layout.tracking)
        , box_style(ts.appearance.box_style)
        , paint(ts.appearance.paint)
        , shadows(ts.appearance.shadows)
        , material(ts.appearance.material)
        , auto_fit(ts.layout.auto_fit)
        , max_lines(ts.layout.max_lines)
        , ellipsis(ts.layout.ellipsis)
        , min_font_size(ts.layout.min_font_size)
        , max_font_size(ts.layout.max_font_size)
        , overflow(ts.layout.overflow)
        , wrap(ts.layout.wrap)
        , pre_shaped(ts.content.pre_shaped)
    {}

    /// Convert to TextSpec (zero-copy for shared_ptr fields, moves strings).
    [[nodiscard]] TextSpec to_spec() const & {
        return TextSpec{
            .content    = {text, pre_shaped},
            .font       = {font_path, font_family, font_weight, font_style, font_size},
            .layout     = {size, anchor, centering_mode, align, vertical_align,
                           wrap, overflow, line_height, tracking, auto_fit,
                           min_font_size, max_font_size, max_lines, ellipsis},
            .appearance = {color, paint, shadows, material, box_style},
            .position   = pos,
        };
    }
    [[nodiscard]] TextSpec to_spec() && {
        return TextSpec{
            .content    = {std::move(text), std::move(pre_shaped)},
            .font       = {std::move(font_path), std::move(font_family), font_weight,
                           std::move(font_style), font_size},
            .layout     = {size, anchor, centering_mode, align, vertical_align,
                           wrap, overflow, line_height, tracking, auto_fit,
                           min_font_size, max_font_size, max_lines, ellipsis},
            .appearance = {color, std::move(paint), std::move(shadows),
                           std::move(material), std::move(box_style)},
            .position   = pos,
        };
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// TextRunSpec — composable text-run descriptor (replaces flat TextRunParams).
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

// ---------------------------------------------------------------------------
// TextRunParams — PR 3 forward-compat for PR 4 (`LayerBuilder::text_run(...)`).
//
// Established stable surface for the upcoming text_run() builder entry point.
// Mirrors the shape of TextParams but adds:
//   - `animators`     : ordered stack of TextAnimatorSpec
//   - `selectors`     : per-animator GlyphSelectors (carried in TextAnimatorSpec
//                       itself; this field is a top-level convenience)
//   - 2.5D-aware geometry (position.z is honoured by the TextRunNode processor)
//
// LayerBuilder::text_run(name, TextRunParams{...}) lands in PR 4 and will:
//   1. Build/cached TextRunLayout (HarfBuzz + FreeType shaping) once
//   2. Evaluate animators per-frame via `evaluate_animator_stack`
//   3. Construct a RenderNode with `is_text_run_shape = true` and
//      `text_run_shape = std::make_shared<TextRunShape>(...)`
//   4. The graph-builder source-pass routes that RenderNode to a TextRunNode
//
// All fields here use defaults that produce a sensible empty run, so PR 3
// scaffolding can include this struct without forcing PR 4 work.
// ---------------------------------------------------------------------------
struct TextRunParams {
    std::string text;
    std::string font_path{"assets/fonts/Inter-Bold.ttf"};
    std::string font_family{"Inter"};
    int font_weight{800};
    std::string font_style{"normal"};
    f32 font_size{72.0f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};

    Vec3 pos{0.0f, 0.0f, 0.0f};        // also drives position.z (2.5D depth)
    Vec2 size{0.0f, 0.0f};             // 0 = intrinsic text wrapping disabled

    TextAnchor anchor{TextAnchor::Center};
    TextAlign align{TextAlign::Center};
    VerticalAlign vertical_align{VerticalAlign::Middle};
    TextWrap wrap{TextWrap::None};
    TextDirection direction{TextDirection::Auto};
    std::string language;              // BCP-47 language tag (auto = empty)

    f32 line_height{1.2f};
    f32 tracking{0.0f};

    TextPaint paint{};
    std::vector<TextShadow> shadows{};
    TextMaterial material{};

    // PR 4 will populate these through a TextRunBuilder fluent chain
    // (see PR 4 follow-up).  Carrying them now avoids API churn when
    // the text_run() builder entry point lands.
    std::vector<TextAnimatorSpec> animators;
    std::vector<GlyphSelectorSpec> selectors;  // optional top-level selectors (renamed from TextSelectorSpec after TextAnimator V2 refactor)

    // Optional pre-shaped glyph run (typewriter-style, contextual scripts).
    std::shared_ptr<PlacedGlyphRun> pre_shaped;

    // Source text cache hint — when a re-evaluation would produce an
    // identical TextRunLayout, the layout cache can skip re-shaping.
    bool cache_layout{true};

    // ── Conversion from TextRunSpec (forward-compat bridge) ──────────
    TextRunParams() = default;
    explicit TextRunParams(const TextRunSpec& rs)
        : text(rs.text.content.value)
        , font_path(rs.text.font.font_path)
        , font_family(rs.text.font.font_family)
        , font_weight(rs.text.font.font_weight)
        , font_style(rs.text.font.font_style)
        , font_size(rs.text.font.font_size)
        , color(rs.text.appearance.color)
        , pos(rs.text.position)
        , size(rs.text.layout.box)
        , anchor(rs.text.layout.anchor)
        , align(rs.text.layout.align)
        , vertical_align(rs.text.layout.vertical_align)
        , wrap(rs.text.layout.wrap)
        , direction(rs.direction)
        , language(rs.language)
        , line_height(rs.text.layout.line_height)
        , tracking(rs.text.layout.tracking)
        , paint(rs.text.appearance.paint)
        , shadows(rs.text.appearance.shadows)
        , material(rs.text.appearance.material)
        , animators(rs.animators)
        , selectors(rs.selectors)
        , pre_shaped(rs.text.content.pre_shaped)
        , cache_layout(rs.cache_layout)
    {}

    [[nodiscard]] TextRunSpec to_spec() const & {
        TextParams tp;
        tp.text        = text;
        tp.size        = size;
        tp.pos         = pos;
        tp.font_path   = font_path;
        tp.font_family = font_family;
        tp.font_weight = font_weight;
        tp.font_style  = font_style;
        tp.font_size   = font_size;
        tp.color       = color;
        tp.anchor      = anchor;
        tp.align       = align;
        tp.vertical_align = vertical_align;
        tp.wrap        = wrap;
        tp.line_height = line_height;
        tp.tracking    = tracking;
        tp.paint       = paint;
        tp.shadows     = shadows;
        tp.material    = material;
        tp.pre_shaped  = pre_shaped;
        return TextRunSpec{
            .text         = tp.to_spec(),
            .direction    = direction,
            .language     = language,
            .animators    = animators,
            .selectors    = selectors,
            .cache_layout = cache_layout,
        };
    }
    [[nodiscard]] TextRunSpec to_spec() && {
        TextParams tp;
        tp.text        = std::move(text);
        tp.size        = size;
        tp.pos         = pos;
        tp.font_path   = std::move(font_path);
        tp.font_family = std::move(font_family);
        tp.font_weight = font_weight;
        tp.font_style  = std::move(font_style);
        tp.font_size   = font_size;
        tp.color       = color;
        tp.anchor      = anchor;
        tp.align       = align;
        tp.vertical_align = vertical_align;
        tp.wrap        = wrap;
        tp.line_height = line_height;
        tp.tracking    = tracking;
        tp.paint       = std::move(paint);
        tp.shadows     = std::move(shadows);
        tp.material    = std::move(material);
        tp.pre_shaped  = std::move(pre_shaped);
        return TextRunSpec{
            .text         = std::move(tp).to_spec(),
            .direction    = direction,
            .language     = std::move(language),
            .animators    = std::move(animators),
            .selectors    = std::move(selectors),
            .cache_layout = cache_layout,
        };
    }
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
