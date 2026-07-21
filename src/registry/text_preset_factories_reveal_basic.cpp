// ─── src/registry/text_preset_factories_reveal_basic.cpp ───────────────────
//
// FASE 1 Step 1 → split (Stage 1) — Reveal-category text-preset factory TU,
// BASIC sub-bucket.  Extracted from `text_preset_factories_reveal.cpp`
// together with the matching `selectors` sub-bucket.  This TU emits the
// 6 simple entrance-animation presets; the 4 selector-driven counterparts
// live in `text_preset_factories_reveal_selectors.cpp`.  The parent
// `text_preset_factories_reveal.cpp` is reduced to a thin aggregator.
//
// Action 12b (this chore): the per-entry boilerplate (PresetMetadata
// construction + fixture string + builder lambda + animator factory pointer)
// is now centralised through the TU-local helper `make_reveal_descriptor`
// declared in the anonymous namespace below.  Per Cat-3 minimal-surface:
// pure internal refactor, zero SDK surface expansion, zero API change
// (the public factory functions `create_basic_reveal_presets()` and the
// 6 entry() functions are bit-identical signature-wise and content-wise).
//
// Each of the 6 entry() invocations now reduces to a single
// `make_reveal_descriptor(metadata, fixture, animator, builder)` call
// with the per-preset values supplied via designated-initialiser
// `PresetMetadata{.id, .display_name, .category, .description, .builtin}`
// + string-literal fixture + named `compose_*` function pointer +
// inline `TextPresetBuilder` lambda.
//
// ## Content (verbatim port from the pre-split file)
//
//   ── REVEAL · BASIC (6) ────────────────────────────────────────────────────
//     1.  text_animations            (PR 41cda40c, kept)
//     2.  fade_in                    (Stage 3)
//     3.  blur_in                    (Stage 3)
//     4.  slide_up                   (Stage 3)
//     5.  slide_down                 (Stage 3)
//     6.  scale_in                   (Stage 3)
//
// ## Architectural invariants (AGENTS.md v0.1 freeze)
//
//  (1) SINGLE central registry.  AnimatorResolver queries ONLY
//      `::chronon3d::registry::builtin_text_preset_registry()`.  This sub-TU
//      contributes to that single registry through the per-category factory
//      surface (M1.5#13 lineage); it does NOT re-implement registration.
//
//  (2) Factory surface is READ-ONLY descriptors.  This file returns
//      `std::vector<TextPresetDescriptor>` from `create_basic_reveal_presets()`;
//      it does NOT call `TextPresetRegistry::register_preset`.
//
//  (3) Uses `::chronon3d::registry::internal::make_presetc_template` and
//      `::chronon3d::registry::internal::wire_through_resolver` from
//      the shared internal helper header.
//
//  (4) Lives in the same `factory_reveal` namespace as the aggregator so
//      the aggregator can call `create_basic_reveal_presets()` unqualified.
//      Different symbol-name (`create_basic_reveal_presets` vs the
//      aggregator's `create_text_presets`) avoids ODR collision inside
//      the shared namespace.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/registry/text_preset_descriptor.hpp>
// PresetMetadata, TextPresetDescriptor, TextPresetCategory, AnimatorFactory.

#include <chronon3d/scene/builders/builder_params.hpp>
// canonical ::chronon3d::TextSpec, TextAnimatorSpec, AnimatedValue<>,
// EasingCurve, Easing, Frame.

#include "text_preset_internal_helpers.hpp"
// internal::make_presetc_template + internal::wire_through_resolver
// + LayerBuilderT / SceneBuilderT / TextSpecT aliases.

#include <string>
#include <vector>

namespace chronon3d::registry::register_helpers_internal::factory_reveal {

using LayerBuilderT  = ::chronon3d::registry::internal::LayerBuilderT;
using SceneBuilderT  = ::chronon3d::registry::internal::SceneBuilderT;
using TextSpecT      = ::chronon3d::registry::internal::TextSpecT;

// ── TU-local helper (Action 12b) ────────────────────────────────────────────
//
// `make_reveal_descriptor(metadata, fixture, animator, builder)` builds a
// `TextPresetDescriptor` from its 4 fields in a single call.  Pass-by-value
// + `std::move` semantics: zero-copy in the typical caller site where the
// args are rvalues (string-literal `fixture`, named `compose_*` function
// pointer, lambda `TextPresetBuilder`).  Lvalue callers also safe (move).
//
// Lives in anonymous namespace INSIDE `factory_reveal` — each TU has its OWN
// internal-linkage version, no ODR collision between TU siblings
// (`text_preset_factories_reveal_selectors.cpp` defines an identical helper
// under its own anonymous namespace).  Zero SDK surface exposure; pure
// extraction-de-boilerplate.
namespace {
TextPresetDescriptor make_reveal_descriptor(
        PresetMetadata metadata,
        std::string fixture,
        AnimatorFactory animator,
        TextPresetBuilder builder) {
    TextPresetDescriptor d;
    d.id               = metadata.id;
    d.metadata         = std::move(metadata);
    d.fixture          = std::move(fixture);
    d.builder          = std::move(builder);
    d.animator_factory = std::move(animator);
    return d;
}
} // namespace (TU-local helper)

// ── STAGE 2 helpers — Reveal · BASIC (6) compositor bodies ─────────────────
//
// Each helper mirrors the corresponding pre-TEXT-RES-01
// `AnimatorResolver::compose_for(...)` branched body verbatim.  Naming
// pattern preserved byte-identical to the pre-extraction anon namespace.

// 1. text_animations — fade_in(20) + scale_drop(0.95,30)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_text_animations(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = ::chronon3d::registry::internal::make_presetc_template("text_animations");
    a.properties.push_back(ScaleProperty{Vec3{0.95f, 0.95f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 2. fade_in — fade_in(15) + soft_pop(10)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_fade_in(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = ::chronon3d::registry::internal::make_presetc_template("fade_in");
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 3. blur_in — focus_in(4.0,30) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_blur_in(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = ::chronon3d::registry::internal::make_presetc_template("blur_in");
    a.properties.push_back(BlurProperty{4.0f});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 4. slide_up — fade_shift_vertical({0,200,0},25) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_slide_up(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = ::chronon3d::registry::internal::make_presetc_template("slide_up");
    a.properties.push_back(PositionProperty{Vec3{0.0f, 200.0f, 0.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 5. slide_down — fade_shift_vertical({0,-200,0},25) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_slide_down(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = ::chronon3d::registry::internal::make_presetc_template("slide_down");
    a.properties.push_back(PositionProperty{Vec3{0.0f, -200.0f, 0.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 6. scale_in — scale_drop(0.85,25) + soft_pop(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_scale_in(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = ::chronon3d::registry::internal::make_presetc_template("scale_in");
    a.properties.push_back(ScaleProperty{Vec3{0.85f, 0.85f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}


// ── 1.  text_animations ───────────────────────────────────────────────────
TextPresetDescriptor text_animations_entry() {
    return make_reveal_descriptor(
        PresetMetadata{
            .id           = "text_animations",
            .display_name = "Text animations utility (typewriter + emphasis)",
            .category     = TextPresetCategory::Reveal,
            .description  = "Reveal-oriented text animation utilities — typewriter "
                             "+ per-glyph emphasis (word pop, scale punch, gradient fill). "
                             "fade_in + scale_drop entrance.",
            .builtin      = true,
        },
        "tests/visual/PR3/pr3_compositions",
        compose_text_animations,
        []([[maybe_unused]] SceneBuilderT& sb,
           LayerBuilderT& lb,
           const TextSpecT& spec) {
            ::chronon3d::registry::internal::wire_through_resolver(lb, "text_animations", spec)
              .fade_in(Frame{20})
              .scale_drop(0.95f, Frame{30});
        });
}

// ── 2.  fade_in ───────────────────────────────────────────────────────────
TextPresetDescriptor fade_in_entry() {
    return make_reveal_descriptor(
        PresetMetadata{
            .id           = "fade_in",
            .display_name = "FadeIn",
            .category     = TextPresetCategory::Reveal,
            .description  = "Pure opacity ramp over Frame{15}, no spatial motion. "
                             "Canonical reveal for body copy / subtitle-level text.",
            .builtin      = true,
        },
        "tests/visual/text/reveal_fade_in",
        compose_fade_in,
        []([[maybe_unused]] SceneBuilderT& sb,
           LayerBuilderT& lb,
           const TextSpecT& spec) {
            ::chronon3d::registry::internal::wire_through_resolver(lb, "fade_in", spec)
              .fade_in(Frame{15})
              .soft_pop(Frame{10});
        });
}

// ── 3.  blur_in ───────────────────────────────────────────────────────────
TextPresetDescriptor blur_in_entry() {
    return make_reveal_descriptor(
        PresetMetadata{
            .id           = "blur_in",
            .display_name = "BlurIn",
            .category     = TextPresetCategory::Reveal,
            .description  = "Focus ramp (4.0 → 0.0 blur) over Frame{30}, paired with "
                             "short fade_in.  Classic motion-graphic reveal pattern.",
            .builtin      = true,
        },
        "tests/visual/text/reveal_blur_in",
        compose_blur_in,
        []([[maybe_unused]] SceneBuilderT& sb,
           LayerBuilderT& lb,
           const TextSpecT& spec) {
            ::chronon3d::registry::internal::wire_through_resolver(lb, "blur_in", spec)
              .focus_in(4.0f, Frame{30})
              .fade_in(Frame{15});
        });
}

// ── 4.  slide_up ──────────────────────────────────────────────────────────
TextPresetDescriptor slide_up_entry() {
    return make_reveal_descriptor(
        PresetMetadata{
            .id           = "slide_up",
            .display_name = "SlideUp",
            .category     = TextPresetCategory::Reveal,
            .description  = "Vertical slide-up (from below, offset {0,200,0}) + "
                             "fade_in over Frame{25}.  Eyebrow / section-header pattern.",
            .builtin      = true,
        },
        "tests/visual/text/reveal_slide_up",
        compose_slide_up,
        []([[maybe_unused]] SceneBuilderT& sb,
           LayerBuilderT& lb,
           const TextSpecT& spec) {
            ::chronon3d::registry::internal::wire_through_resolver(lb, "slide_up", spec)
              .fade_shift_vertical(Vec3{0.0f, 200.0f, 0.0f}, Frame{25})
              .fade_in(Frame{15});
        });
}

// ── 5.  slide_down ────────────────────────────────────────────────────────
TextPresetDescriptor slide_down_entry() {
    return make_reveal_descriptor(
        PresetMetadata{
            .id           = "slide_down",
            .display_name = "SlideDown",
            .category     = TextPresetCategory::Reveal,
            .description  = "Vertical slide-down (from above, offset {0,-200,0}) + "
                             "fade_in over Frame{25}.  Bottom-docked subtitle entry.",
            .builtin      = true,
        },
        "tests/visual/text/reveal_slide_down",
        compose_slide_down,
        []([[maybe_unused]] SceneBuilderT& sb,
           LayerBuilderT& lb,
           const TextSpecT& spec) {
            ::chronon3d::registry::internal::wire_through_resolver(lb, "slide_down", spec)
              .fade_shift_vertical(Vec3{0.0f, -200.0f, 0.0f}, Frame{25})
              .fade_in(Frame{15});
        });
}

// ── 6.  scale_in ──────────────────────────────────────────────────────────
TextPresetDescriptor scale_in_entry() {
    return make_reveal_descriptor(
        PresetMetadata{
            .id           = "scale_in",
            .display_name = "ScaleIn",
            .category     = TextPresetCategory::Reveal,
            .description  = "Scale drop (0.85 → 1.0) over Frame{25} + soft_pop "
                             "settle.  Section-header crop pattern.",
            .builtin      = true,
        },
        "tests/visual/text/reveal_scale_in",
        compose_scale_in,
        []([[maybe_unused]] SceneBuilderT& sb,
           LayerBuilderT& lb,
           const TextSpecT& spec) {
            ::chronon3d::registry::internal::wire_through_resolver(lb, "scale_in", spec)
              .scale_drop(0.85f, Frame{25})
              .soft_pop(Frame{15});
        });
}


// ── public factory surface (sub-bucket: BASIC) ─────────────────────────────
//
// `create_basic_reveal_presets()` returns the 6 Reveal · BASIC descriptors
// in canonical Reveal insertion order (basic precedes the 4 selectors
// emitted by `create_selector_reveal_presets()`).
[[nodiscard]] std::vector<TextPresetDescriptor>
create_basic_reveal_presets() {
    return {
        text_animations_entry(),
        fade_in_entry(),
        blur_in_entry(),
        slide_up_entry(),
        slide_down_entry(),
        scale_in_entry(),
    };
}

} // namespace chronon3d::registry::register_helpers_internal::factory_reveal
