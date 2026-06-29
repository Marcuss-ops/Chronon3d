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
//   The remaining variants (RotationProperty, SkewProperty, AnchorProperty,
//   FillColorProperty, StrokeColorProperty, StrokeWidthProperty,
//   BaselineShiftProperty, CharacterOffsetProperty) keep static values.
//   They can be migrated in a follow-up PR without breaking any contracts.

struct PositionProperty {
    AnimatedValue<Vec3> value{Vec3{0.0f, 0.0f, 0.0f}};
};

struct ScaleProperty {
    AnimatedValue<Vec3> value{Vec3{1.0f, 1.0f, 1.0f}};
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
    AnimatedValue<f32> value{1.0f};
};

struct BlurProperty {
    AnimatedValue<f32> radius{0.0f};           // gaussian blur radius in pixels
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
    AnimatedValue<f32> pixels{0.0f};          // extra tracking per glyph in pixels
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

} // namespace chronon3d
