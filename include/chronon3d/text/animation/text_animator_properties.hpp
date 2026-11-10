#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>

#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// PropertyPhase — which pipeline stage a property belongs to
// ═══════════════════════════════════════════════════════════════════════════
//
// FASE 2a (TXT-Verified): CharacterOffset/CharacterValue modify source
// text and MUST be evaluated BEFORE shaping, because different code
// points produce different glyphs, metrics, clusters, ligatures, and
// bidi reordering.  Applying them after layout is architecturally wrong.
//
//   PreShaping  — modifies source text → invalidates shaping + layout
//   PreLayout   — modifies text properties that affect paragraph composition
//   PostLayout  — modifies per-glyph state after layout (GlyphInstanceState)
//   PostRaster  — effects applied after rasterization

enum class PropertyPhase {
    PreShaping,
    PreLayout,
    PostLayout,
    PostRaster
};


/// Return the phase for each property type at compile time.
/// CharacterOffsetProperty and CharacterValueProperty are the PreShaping
/// properties; text substitutions beyond these remain future work.

// Forward-declared here so the template body can reference them;
// definitions follow below in the property structs section.
struct TrackingProperty;
struct CharacterOffsetProperty;
struct CharacterValueProperty;


// ═══════════════════════════════════════════════════════════════════════════
// TextPropertyBlendMode — composition mode for property evaluation
// ═══════════════════════════════════════════════════════════════════════════
//
// Controls how multiple animators of the same property type compose:
//   Add      — sum the weighted values (default for transforms)
//   Replace  — last animator wins (default for colors)
//   Multiply — multiply the weighted values
//
// Owned by this header; `TextAnimatorSpec::transform_mode` and
// `TextAnimatorSpec::color_mode` reference this type.

enum class TextPropertyBlendMode {
    Add,
    Replace,
    Multiply
};

// ═══════════════════════════════════════════════════════════════════════════
// Text animatable property structs
// ═══════════════════════════════════════════════════════════════════════════
//
// Each property describes a target value that should be applied to a glyph.
// The actual amount applied is scaled by the selector weight at frame time.
//
// AGENT 2 — Testi realmente animati (TICKET-A2):
//   PositionProperty, ScaleProperty, OpacityProperty, BlurProperty, and
//   TrackingProperty now hold `AnimatedValue<T>` instead of static `T` so
//   the per-frame evaluator in `src/text/animation/text_property_applier.cpp`
//   can query `.evaluate(time)`. The behaviour for callers that constructed
//   properties with `{1.0f}` / `Vec3{1,1,1}` / etc. is preserved —
//   aggregate-init still routes through `AnimatedValue(T default_value)`
//   so the static end-state semantics are equivalent when no keyframes are
//   present (AnimatedValue evaluates to m_default_value).
//
//   Rotation, fill, skew, anchor, baseline shift, background and stroke
//   properties use the same representation so V3 tracks can be evaluated at
//   the requested sample time. CharacterOffsetProperty is also animated; its
//   sampled value is applied before shaping.

struct PositionProperty {
    AnimatedValue<Vec3> value{Vec3{0.0f, 0.0f, 0.0f}};
};

struct ScaleProperty {
    AnimatedValue<Vec3> value{Vec3{1.0f, 1.0f, 1.0f}};
};

struct RotationProperty {
    AnimatedValue<Vec3> degrees{Vec3{0.0f, 0.0f, 0.0f}};  // X, Y, Z rotation in degrees
};

// Run-level directional light for the composed GEL text material. The vector
// is normalized when sampled; its XY angle selects azimuth and Z selects the
// normalized light elevation consumed by the Vulkan material shader.
struct GelLightDirectionProperty {
    AnimatedValue<Vec3> direction{Vec3{0.0f, 0.0f, 1.0f}};
};

struct SkewProperty {
    AnimatedValue<f32> angle{0.0f};   // skew angle in degrees
    AnimatedValue<f32> axis{0.0f};    // skew axis angle in degrees
    bool animate_angle{true};
    bool animate_axis{true};
};

struct AnchorProperty {
    AnimatedValue<Vec3> value{Vec3{0.0f, 0.0f, 0.0f}}; // anchor point offset
};

struct OpacityProperty {
    AnimatedValue<f32> value{1.0f};
};

struct BlurProperty {
    AnimatedValue<f32> radius{0.0f};           // gaussian blur radius in pixels
};

struct FillColorProperty {
    AnimatedValue<Color> color{Color{1.0f, 1.0f, 1.0f, 1.0f}};
};

struct BackgroundColorProperty {
    AnimatedValue<Color> color{Color{0.0f, 0.0f, 0.0f, 0.0f}};
    AnimatedValue<f32> padding_em{0.5f};
    AnimatedValue<f32> radius_em{0.0f};
    bool animate_color{true};
    bool animate_padding{true};
    bool animate_radius{true};
};

struct StrokeColorProperty {
    AnimatedValue<Color> color{Color{0.0f, 0.0f, 0.0f, 1.0f}};
};

struct StrokeWidthProperty {
    AnimatedValue<f32> width{0.0f};
};

struct TrackingProperty {
    /// FASE 2b: visual-only tracking — adjusts glyph advances post-layout.
    /// This does NOT affect HarfBuzz shaping, line breaking, or justification.
    /// For layout-affecting tracking, use `TextLayoutSpec::tracking` which
    /// feeds into the shaping engine directly.
    AnimatedValue<f32> pixels{0.0f};          // extra tracking per glyph in pixels
};

struct BaselineShiftProperty {
    AnimatedValue<f32> pixels{0.0f};  // vertical shift from baseline in pixels
};

/// PostLayout word gap, in pixels, inserted after each selected word.
/// The gap is cumulative across the run and does not trigger line breaking
/// or re-shaping; it mutates GlyphInstanceState::position.x after layout.
struct WordSpacingProperty {
    AnimatedValue<f32> pixels{0.0f};
};

/// PostLayout line gap, in pixels, inserted after each selected line.
/// Line grouping comes from GlyphSelectorUnitMap; paragraph composition is
/// intentionally unchanged.
struct LineSpacingProperty {
    AnimatedValue<f32> pixels{0.0f};
};

/// P1-04 — run-level vertical anchor for the whole text block.  The shift is
/// measured from the first-line baseline (glyph y = 0) and applied uniformly
/// to every glyph in PostLayout.  Line Spacing keeps line 0 at zero, so the
/// two properties compose without shifting line 0 twice.
enum class LineAnchorMode : u8 {
    Top = 0,     // top of the block (baseline - ascent) at the origin
    Center = 1,  // vertical centre of the whole block at the origin
    Bottom = 2,  // bottom of the last line (block height - ascent) at the origin
};

struct LineAnchorProperty {
    LineAnchorMode mode{LineAnchorMode::Top};
};

/// After Effects-style Character Offset: shifts the character value
/// (code point) by a sampled amount.  The animated value is evaluated at
/// the frame time and rounded to an integer codepoint shift.  Wraps within
/// the alphanumeric range.  Example: offset=1 turns 'A' into 'B', 'Z'
/// into 'A'.
///
/// FASE 2a: CharacterOffsetProperty is now evaluated in the PreShaping
/// phase — the source text is transformed BEFORE HarfBuzz shaping so
/// glyph selection, kerning, ligatures, and bidi reordering all see the
/// offset characters.  See `apply_character_offset_to_source()` in
/// src/text/animation/text_pre_shaping.cpp.
///
/// Previously this was applied to GlyphInstanceState.character_offset
/// AFTER layout (which was architecturally wrong — the comment below
/// has always stated "must be applied BEFORE TextRunLayout is built").
struct CharacterOffsetProperty {
    AnimatedValue<f32> offset{0.0f};   // sampled and rounded codepoint shift
};

/// P1-01 — After Effects-style Character Value.  Every non-whitespace source
/// code point is replaced by the sampled Unicode scalar before shaping, so
/// metrics, clusters, ligatures and bidi reordering all see the substituted
/// text.  The animated value is rounded per frame; 0 and any value that is not
/// a Unicode scalar mean "leave the source unchanged".
///
/// Like CharacterOffsetProperty this is run-level: selectors are resolved
/// after shaping, so they cannot scope a PreShaping substitution yet.  ASCII
/// whitespace (space, tab, CR, LF, FF, VT) is preserved so paragraph and line
/// structure survive the effect.
struct CharacterValueProperty {
    AnimatedValue<f32> codepoint{0.0f};  // sampled and rounded Unicode scalar
};

// ── P1-03 — per-character alignment ─────────────────────────────────────
// Aligns each selected glyph cell to the left, centre or right edge of the
// layout line that contains it.  PostLayout: the line extents come from the
// authored PlacedGlyphRun and no reshaping or reflow happens.
enum class CharacterAlignmentMode : u8 {
    Left = 0,
    Center = 1,
    Right = 2,
};

struct CharacterAlignmentProperty {
    CharacterAlignmentMode mode{CharacterAlignmentMode::Left};
};

// ── P1-05 — anchor point grouping ───────────────────────────────────────
// Selects the pivot that scale/rotation use for every glyph of the animator:
// the glyph itself, its word box, its line box or the whole run.  The
// evaluator resolves the pivot once per animator and writes it into
// GlyphInstanceState::anchor, which is the pivot both the software matrix and
// the native glyph placement already honour.
enum class AnchorPointGrouping : u8 {
    Character = 0,
    Word = 1,
    Line = 2,
    All = 3,
};

struct AnchorPointGroupingProperty {
    AnchorPointGrouping mode{AnchorPointGrouping::Character};
};

// ── P1-06 / P1-07 — per-glyph paint animators ───────────────────────────
// These properties write GlyphInstanceState::shadow / glow / fill_gradient.
// They are PostLayout (visual-only); no re-shaping or reflow is involved.
struct GlyphShadowColorProperty {
    AnimatedValue<Color> color{Color{0.0f, 0.0f, 0.0f, 1.0f}};
};

struct GlyphShadowOffsetProperty {
    AnimatedValue<Vec3> translation{Vec3{0.0f, 0.0f, 0.0f}};  // z unused
};

struct GlyphShadowBlurProperty {
    AnimatedValue<f32> radius{0.0f};
};

struct GlyphShadowOpacityProperty {
    AnimatedValue<f32> opacity{1.0f};
};

struct GlyphGlowColorProperty {
    AnimatedValue<Color> color{Color{1.0f, 1.0f, 1.0f, 1.0f}};
};

struct GlyphGlowRadiusProperty {
    AnimatedValue<f32> radius{0.0f};
};

struct GlyphGlowIntensityProperty {
    AnimatedValue<f32> intensity{1.0f};
};

struct GlyphGradientStartColorProperty {
    AnimatedValue<Color> color{Color{1.0f, 1.0f, 1.0f, 1.0f}};
};

struct GlyphGradientEndColorProperty {
    AnimatedValue<Color> color{Color{0.0f, 0.0f, 0.0f, 1.0f}};
};

struct GlyphGradientAngleProperty {
    AnimatedValue<f32> degrees{0.0f};
};

// ── P2-05 — reflow animators (PreLayout) ────────────────────────────────
// Unlike WordSpacingProperty/LineSpacingProperty (PostLayout, visual-only),
// these feed the paragraph composer and rebuild the layout when they change.
// They are evaluated by the driver before PostLayout animators run.
struct ReflowWordSpacingProperty {
    AnimatedValue<f32> pixels{0.0f};
};

struct ReflowLineSpacingProperty {
    AnimatedValue<f32> pixels{0.0f};
};

// ═══════════════════════════════════════════════════════════════════════════
// TextAnimatorProperty — variant covering all supported property types
// ═══════════════════════════════════════════════════════════════════════════
//
// The single canonical property vector type. The dispatcher in
// `src/text/animation/text_property_applier.cpp` uses `std::visit` against
// this variant — there is exactly ONE place where per-property logic lives,
// regardless of how many property types are added.

using TextAnimatorProperty = std::variant<
    PositionProperty,
    ScaleProperty,
    RotationProperty,
    GelLightDirectionProperty,
    SkewProperty,
    AnchorProperty,
    OpacityProperty,
    BlurProperty,
    FillColorProperty,
    BackgroundColorProperty,
    StrokeColorProperty,
    StrokeWidthProperty,
    TrackingProperty,
    BaselineShiftProperty,
    CharacterOffsetProperty,
    WordSpacingProperty,
    LineSpacingProperty,
    GlyphShadowColorProperty,
    GlyphShadowOffsetProperty,
    GlyphShadowBlurProperty,
    GlyphShadowOpacityProperty,
    GlyphGlowColorProperty,
    GlyphGlowRadiusProperty,
    GlyphGlowIntensityProperty,
    GlyphGradientStartColorProperty,
    GlyphGradientEndColorProperty,
    GlyphGradientAngleProperty,
    ReflowWordSpacingProperty,
    ReflowLineSpacingProperty,
    LineAnchorProperty,
    CharacterValueProperty,
    CharacterAlignmentProperty,
    AnchorPointGroupingProperty
>;

} // namespace chronon3d
