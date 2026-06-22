#pragma once

#include <chronon3d/text/glyph_selector.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>

#include <string>
#include <variant>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextAnimatorProperty — per-glyph animatable properties
// ═══════════════════════════════════════════════════════════════════════════
//
// Each property describes a target value that should be applied to a glyph.
// The actual amount applied is scaled by the selector weight.
//
// Blend modes control how multiple animators of the same type compose:
//   Add      — sum the weighted values (default for transforms)
//   Replace  — last animator wins (default for colors)
//   Multiply — multiply the weighted values

enum class TextPropertyBlendMode {
    Add,
    Replace,
    Multiply
};

// ── Individual property structs ───────────────────────────────────────────

struct PositionProperty {
    Vec3 value{0.0f, 0.0f, 0.0f};
};

struct ScaleProperty {
    Vec3 value{1.0f, 1.0f, 1.0f};
};

struct RotationProperty {
    Vec3 degrees{0.0f, 0.0f, 0.0f};  // X, Y, Z rotation in degrees
};

struct SkewProperty {
    f32 angle{0.0f};                  // skew angle in degrees
    f32 axis{0.0f};                   // skew axis angle in degrees
};

struct AnchorProperty {
    Vec3 value{0.0f, 0.0f, 0.0f};    // anchor point offset
};

struct OpacityProperty {
    f32 value{1.0f};
};

struct BlurProperty {
    f32 radius{0.0f};                 // gaussian blur radius in pixels
};

struct FillColorProperty {
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct StrokeColorProperty {
    Color color{0.0f, 0.0f, 0.0f, 1.0f};
};

struct StrokeWidthProperty {
    f32 width{0.0f};
};

struct TrackingProperty {
    f32 pixels{0.0f};                 // extra tracking per glyph in pixels
};

struct BaselineShiftProperty {
    f32 pixels{0.0f};                 // vertical shift from baseline in pixels
};

/// After Effects-style Character Offset: shifts the character value
/// (code point) by a fixed amount.  Wraps within the alphanumeric range.
/// Example: offset=1 turns 'A' into 'B', 'Z' into 'A'.
/// When enabled, the per-glyph character values are offset before shaping.
/// The active text content is modified, so this must be applied BEFORE
/// the TextRunLayout is built (layout depends on the offset characters).
struct CharacterOffsetProperty {
    i32 offset{0};                     // number of codepoints to shift
};

// ── Variant: any supported text animator property ─────────────────────────

using TextAnimatorProperty = std::variant<
    PositionProperty,
    ScaleProperty,
    RotationProperty,
    SkewProperty,
    AnchorProperty,
    OpacityProperty,
    BlurProperty,
    FillColorProperty,
    StrokeColorProperty,
    StrokeWidthProperty,
    TrackingProperty,
    BaselineShiftProperty,
    CharacterOffsetProperty
>;

// ═══════════════════════════════════════════════════════════════════════════
// TextAnimatorSpec — a single animator = selectors + properties
// ═══════════════════════════════════════════════════════════════════════════

struct TextAnimatorSpec {
    std::string id;                               // unique id for diagnostics
    bool enabled{true};

    std::vector<GlyphSelectorSpec> selectors;     // one or more selectors
    std::vector<TextAnimatorProperty> properties; // properties to animate

    TextPropertyBlendMode transform_mode{TextPropertyBlendMode::Add};
    TextPropertyBlendMode color_mode{TextPropertyBlendMode::Replace};
};

// ═══════════════════════════════════════════════════════════════════════════
// TextAnimatorStack — canonical animation-stack type
// ═══════════════════════════════════════════════════════════════════════════
//
// The canonical text-animation-stack is an ordered list of TextAnimatorSpec
// entries, evaluated in sequence by `evaluate_animator_stack(...)` below.
// This typedef closes the canonical-contract gap documented in
// `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §"Pipeline canonica":
//
//     TextDocument → TextResolver → FontEngine → HarfBuzz shaping
//     → TextLayoutEngine → TextRunLayout → GlyphInstanceState
//     → TextAnimatorStack → TextRunNode → RenderBackend
//
// The first three canonical names already lived in dedicated headers:
//   • TextDocument      (include/chronon3d/text/text_document.hpp)
//   • TextRunLayout     (include/chronon3d/text/text_run.hpp)
//   • GlyphInstanceState (this header, below)
//
// "TextAnimatorStack" was previously a doc-only concept that pointed at
// the `std::vector<TextAnimatorSpec>` field of `TextRunShape`
// (`include/chronon3d/text/text_run.hpp::TextRunShape::animators`). PR-A2
// promotes it to an explicit named type so new code can write
// `TextAnimatorStack` against this header without depending on
// `TextRunShape`'s internal layout. Backward-compatible: it is a
// typedef over `std::vector<TextAnimatorSpec>` so existing code that
// uses the vector form continues to compile unchanged.
//
// Default-constructible, sharable across frames (the canonical pipeline
// never mutates the stack after build; `evaluate_animator_stack(...)`
// reads it immutably per-frame).
using TextAnimatorStack = std::vector<TextAnimatorSpec>;

// ═══════════════════════════════════════════════════════════════════════════
// GlyphInstanceState — resolved per-glyph state for one frame
// ═══════════════════════════════════════════════════════════════════════════
//
// This is the output of evaluating all animators for a single glyph.
// The renderer uses this to position and style each glyph in a TextRun.

struct GlyphInstanceState {
    u32 glyph_id{0};                  // font-specific glyph index

    Vec2 layout_position{0.0f, 0.0f}; // base position from text layout
    Vec3 position{0.0f, 0.0f, 0.0f};  // animated position offset
    Vec3 scale{1.0f, 1.0f, 1.0f};     // animated scale
    Vec3 rotation{0.0f, 0.0f, 0.0f};  // animated rotation (degrees, XYZ)
    Vec3 anchor{0.0f, 0.0f, 0.0f};    // animated anchor point

    f32 skew{0.0f};                    // skew angle
    f32 skew_axis{0.0f};              // skew axis angle
    f32 opacity{1.0f};                // final opacity
    f32 blur{0.0f};                   // blur radius
    f32 baseline_shift{0.0f};         // vertical shift from baseline
    i32 character_offset{0};          // code-point offset (CharacterOffsetProperty)

    Color fill{1.0f, 1.0f, 1.0f, 1.0f};   // final fill color
    Color stroke{0.0f, 0.0f, 0.0f, 0.0f};  // stroke color (alpha=0 → disabled)
    f32 stroke_width{0.0f};                 // stroke width
};

// ═══════════════════════════════════════════════════════════════════════════
// Animator stack evaluation
// ═══════════════════════════════════════════════════════════════════════════

/// Build initial glyph states from a PlacedGlyphRun.
/// Each glyph gets its layout_position from the placed run.
[[nodiscard]] std::vector<GlyphInstanceState> make_initial_glyph_states(
    const PlacedGlyphRun& placed
);

/// Evaluate a single animator spec for all glyphs, mutating their states.
/// @param spec         The animator specification (selectors + properties).
/// @param unit_map     Pre-built unit index map for the text run.
/// @param glyph_states Mutable glyph states (modified in-place).
/// @param source       Original source text.
/// @param time         Sample time for AnimatedValue evaluation.
void evaluate_animator(
    const TextAnimatorSpec& spec,
    const TextUnitMap& unit_map,
    std::vector<GlyphInstanceState>& glyph_states,
    std::string_view source,
    SampleTime time
);

/// Evaluate a full animator stack for all glyphs.
/// Processes animators in order, applying blend modes.
/// @param animators    Ordered list of animator specs.
/// @param placed       The resolved glyph run (for initial layout).
/// @param source       Original source text.
/// @param time         Sample time for evaluation.
/// @return Final glyph states ready for rendering.
[[nodiscard]] std::vector<GlyphInstanceState> evaluate_animator_stack(
    const std::vector<TextAnimatorSpec>& animators,
    const PlacedGlyphRun& placed,
    std::string_view source,
    SampleTime time
);

/// Re-evaluate the full animator stack, writing per-glyph state back
/// into the provided `inout_states` vector. Semantically equivalent to
/// `evaluate_animator_stack` but allocates no return value — used by
/// the per-frame animation driver (PR 8) so consecutive frames don't
/// allocate a new vector.
///
/// The vector is REPLACED with freshly-initialised states for
/// `placed.glyphs.size()` glyphs via `make_initial_glyph_states`,
/// then each animator mutates them in place.  Callers must NOT
/// preserve the previous contents across calls.
void evaluate_animator_stack_into(
    std::vector<GlyphInstanceState>& inout_states,
    const std::vector<TextAnimatorSpec>& animators,
    const PlacedGlyphRun& placed,
    std::string_view source,
    SampleTime time
);

} // namespace chronon3d
