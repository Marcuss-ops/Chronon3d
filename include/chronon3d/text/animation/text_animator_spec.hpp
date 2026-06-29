#pragma once

// North→south dependency: properties (TextPropertyBlendMode +
// TextAnimatorProperty variant) feeds into TextAnimatorSpec.
#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/text/glyph_selector.hpp>                       // GlyphSelectorSpec

#include <string>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextAnimatorSpec — a single animator = selectors + properties
// ═══════════════════════════════════════════════════════════════════════════
//
// One instance is the canonical "AE-style animator entry" carried through
// the text pipeline. Engineered at authoring time (MotionRegistry,
// Animator DSL, or hand-rolled) and consumed unchanged by the per-frame
// evaluator (`evaluate_animator` in text_animator_evaluator.cpp).
//
// Two independent blend modes by side, mirroring After Effects:
//   - `transform_mode` controls how transform-type properties compose
//     (Position / Scale / Rotation / Skew / Anchor / Opacity / Blur /
//      BaselineShift / CharacterOffset / Tracking). Default Add.
//   - `color_mode` controls how color properties compose (FillColor /
//     StrokeColor). Default Replace (last-writer-wins for colors).

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
// entries, evaluated in sequence by `evaluate_animator_stack(...)` in
// `include/chronon3d/text/animation/text_animator_stack.hpp`. This typedef
// closes the canonical-contract gap documented in
// `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §"Pipeline canonica":
//
//     TextDocument → TextResolver → FontEngine → HarfBuzz shaping
//     → TextLayoutEngine → TextRunLayout → GlyphInstanceState
//     → TextAnimatorStack → TextRunNode → RenderBackend
//
// "TextAnimatorStack" was previously a doc-only concept that pointed at
// the `std::vector<TextAnimatorSpec>` field of `TextRunShape`
// (`include/chronon3d/text/text_run.hpp::TextRunShape::animators`). It is
// a typedef over `std::vector<TextAnimatorSpec>` so existing code that
// uses the vector form continues to compile unchanged.
//
// Default-constructible, sharable across frames (the canonical pipeline
// never mutates the stack after build; `evaluate_animator_stack(...)`
// reads it immutably per-frame).

using TextAnimatorStack = std::vector<TextAnimatorSpec>;

} // namespace chronon3d
