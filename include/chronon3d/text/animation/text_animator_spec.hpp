#pragma once

// North→south dependency: properties (TextPropertyBlendMode +
// TextAnimatorProperty variant) feeds into TextAnimatorSpec.
#include <chronon3d/text/animation/text_animator_properties.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>              // GlyphSelectorSpec (TICKET-046 Phase 1a)

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

    // ─────────────────────────────────────────────────────────────────────
    // §5.0a — compile() + §5.0e — is_valid() chain-method pair
    // ─────────────────────────────────────────────────────────────────────
    //
    // Closes the user-spec `resolve().compile().is_valid()` gap documented
    // in `docs/CHANGELOG.md` §5 (gap explicitly tracked there since the
    // MotionTimeline<T> declarator landed in the F3.A batch).  The pair
    // enables a fluent authoring pattern:
    //
    //     AuthoringSite::build_animator()
    //         .resolve_selectors(...)
    //         .compile()       // self-ref; normalizes internal state
    //         .is_valid()      // ≥5 invariants checked
    //         .commit()
    //
    // Implementation lives in
    // `src/text/animation/text_animator_compile.cpp` (an OBJECT FILE,
    // not a singleton/registry — see AGENTS.md v0.1 Cat-5 invariant).
    //
    //   compile()  — self-reference return for fluent chaining.  No
    //                heap allocation; no public struct members added;
    //                AGENTS.md Cat-3 zero-new-SDK-symbol compliance
    //                (existing struct, two new methods on it).
    //   is_valid() — 4 invariants beyond empty/empty membership:
    //                (Inv 1) non-empty selectors + non-empty properties
    //                        (this IS the "membership predicate" the user
    //                        referenced the chain-method pair as breaking);
    //                (Inv 2) strict monotonicity: AnimatedValue keyframe
    //                        frames strictly increase (no duplicates) —
    //                        locks against the "add_keyframe twice at frame N"
    //                        footgun.  This is the invariant that breaks
    //                        the membership-predicate ceiling — a single
    //                        empty-check is insufficient to distinguish a
    //                        monotonic time-curve from a degenerate duplicate-
    //                        frame one.
    //                (Inv 3) value integrity: no NaN/Inf in any keyframe
    //                        value OR any static-value property field —
    //                        locks against the "0/0 in normalized scale"
    //                        + "infinite-range frame timestamp" + "NaN
    //                        width/angle" footguns;
    //                (Inv 4) blend-mode coverage: transform_mode + color_mode
    //                        are scoped enums; explicit value-comparison
    //                        against {Add, Replace, Multiply} makes the
    //                        contract machine-verifiable.

    [[nodiscard]] TextAnimatorSpec& compile();
    [[nodiscard]] bool is_valid() const;
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
