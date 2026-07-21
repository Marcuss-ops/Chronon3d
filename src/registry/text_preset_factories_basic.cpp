// ─── src/registry/text_preset_factories_basic.cpp ──────────────────────────
//
// M1.5#13 (1/4 — Subtitle/basic category extraction) — per-category
// text-preset factory TU.
//
// ## Architectural invariants (AGENTS.md v0.1 freeze)
//
//  (1) SINGLE central registry.  AnimatorResolver queries ONLY
//      `chronon3d::registry::builtin_text_preset_registry()` (per
//      ANTI_DUPLICATION_RULES.md §registry/resolver).  This file does
//      NOT introduce a new registry, preset type, sampler, or catalog.
//
//  (2) Factory surface is READ-ONLY descriptors.  This file returns
//      `std::vector<TextPresetDescriptor>` from
//      `create_text_presets()`; it does NOT call
//      `TextPresetRegistry::register_preset`.  Registration is the
//      sole responsibility of
//      `register_helpers_internal::register_text_preset_basic` (a
//      delegate of `register_text_preset_subtitle` in
//      `src/registry/text_preset_registry.cpp`).
//
//  (3) The 4 factories (basic / kinetic / cinematic / social) emitted
//      across the M1.5#13 4-commit sequence share the
//      internal helper header
//      `text_preset_internal_helpers.hpp` for `make_presetc_template`
//      + `wire_through_resolver`.  Each factory's preamble helpers
//      (the `compose_<id>_animator_compose` closures) live in the
//      per-category TU because they encode category-specific id
//      dispatch — sharing them would re-create the per-id table that
//      AnimatorResolver was refactored to remove (STAGE 2).
//
// ## Content (verbatim port from src/registry/text_preset_registry.cpp)
//
//   ── SUBTITLE (4) — Stage 3 ──────────────────────────────────────────────
//     19. minimal_white             (no motion; compositor returns nullopt)
//     20. yellow_keyword            (word_stagger + fade_in)
//     21. glow_pulse                (continuous tracking-breathing idle)
//     22. caption_box               (short downward dock + fade_in)
//
//   Each entry exposes 5 fields per the TEXT-RES-01 spec:
//     { id, metadata{id,display_name,category,description,builtin},
//       builder, animator_factory, fixture }.
//
// ## File-local taxonomy
//
//   compose_<id>_animator_compose(meta) — pure-function compositor
//                                          (returns std::optional<TextAnimatorSpec>).
//                                          Pattern: starts from `make_presetc_template`,
//                                          appends entry-specific properties / selector
//                                          overrides per the pre-M1.5#13 verbatim bodies.
//
//   <id>_entry()                        — factory body returning a
//                                          fully populated TextPresetDescriptor with
//                                          { id, metadata, builder (LayerBuilder-side
//                                          closure), animator_factory, fixture }.
//
//   create_text_presets()               — public surface for this factory TU:
//                                          returns std::vector<TextPresetDescriptor>
//                                          of length 4 in the canonical Subtitle order.
//
// ## Naming
//
//   `create_text_presets()` (no `basic` qualifier) — naming chosen for
//   forward-compatibility: future steps in M1.5#13 add
//   text_preset_factories_kinetic.cpp / _cinematic.cpp / _social.cpp,
//   each exporting the SAME symbol-name `create_text_presets()` in its
//   own category-namespaced TU.  Callers disambiguate via
//   `chronon3d::registry::register_helpers_internal::factory_basic()` +
//   ..._factory_kinetic() etc.  This name-collision intent is intentional:
//   each TU has its own anon namespace so the symbol does not collide at
//   link time; the printable disambiguator comes from the namespace
//   path the caller resolves through.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/registry/text_preset_descriptor.hpp>
// PresetMetadata, TextPresetDescriptor, TextPresetCategory, AnimatorFactory.

#include <chronon3d/scene/builders/builder_params.hpp>
// canonical ::chronon3d::TextSpec (via internal::TextSpecT alias),
// TextAnimatorSpec, AnimatedValue<>, EasingCurve, Easing.

#include "text_preset_internal_helpers.hpp"
// internal::make_presetc_template + internal::wire_through_resolver
// + LayerBuilderT / SceneBuilderT / TextSpecT aliases.

#include <optional>
#include <string>
#include <vector>

namespace chronon3d::registry::register_helpers_internal::factory_basic {

using LayerBuilderT  = chronon3d::registry::internal::LayerBuilderT;
using SceneBuilderT  = chronon3d::registry::internal::SceneBuilderT;
using TextSpecT      = chronon3d::registry::internal::TextSpecT;

// ── STAGE 2 helpers — Subtitle (4) compositor bodies (verbatim port) ─────
//
// Each closure mirrors the corresponding pre-TEXT-RES-01
// `AnimatorResolver::compose_for(...)` branched body verbatim.  Naming
// pattern preserved byte-identical to the pre-M1.5#13 anon namespace.

// 19. minimal_white — no-motion baseline.  Returns std::nullopt so the
// LayerBuilder-side call wires `lb.text_run` with `params.animators.empty()`;
// the TextRunShape driver treats it as a static no-op (Sub-case 7-9 invariant).
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_minimal_white(const PresetMetadata& /*meta*/) {
    return std::nullopt;
}

// 20. yellow_keyword — fade_in(12).  Word-stagger frame sequence lives on the
// layer clock via `.word_stagger(Frame{3}, Frame{20})` in the builder body.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_yellow_keyword(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("yellow_keyword");
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 21. glow_pulse — continuous tracking-breathing idle (Frame{40}).
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_glow_pulse(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("glow_pulse");
    a.properties.push_back(TrackingProperty{0.08f});
    return a;
}

// 22. caption_box — fade_in(12).  Vertical dock offset lives on the layer
// clock via `.fade_shift_vertical({0,-30,0}, Frame{18})` in the builder body.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_caption_box(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("caption_box");
    // No hard-coded positional offset: the preset relies on the
    // caller-supplied semantic placement (SafeAreaBottom / center) so that
    // layout/ink/effect bounds stay geometrically consistent.
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}


// 23. karaoke_fill — word-level fill-colour sweep.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_karaoke_fill(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("karaoke_fill");
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 24. active_word_pop — scale punch.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_active_word_pop(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("active_word_pop");
    a.properties.push_back(ScaleProperty{Vec3{1.15f, 1.15f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 25. subtitle_card — rounded background card.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_subtitle_card(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("subtitle_card");
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 26. lower_third_safe — lower-third dock.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_lower_third_safe(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("lower_third_safe");
    // No hard-coded positional offset: the preset relies on the
    // caller-supplied semantic placement (SafeAreaBottom / center) so that
    // layout/ink/effect bounds stay geometrically consistent.
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// ── 19. minimal_white ─────────────────────────────────────────────────────
TextPresetDescriptor minimal_white_entry() {
    PresetMetadata meta;
    meta.id           = "minimal_white";
    meta.display_name = "MinimalWhite";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "No-motion baseline. Routes through `wire_through_resolver` "
                         "(single canonical path) — empty animators produce a "
                         "static TextRunNode via `materialize_text_run_shape`.  "
                         "Use for static captions or burnt-in subs.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_minimal_white";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        // P1 — minimal_white now goes through the same path as the other
        // 21 built-ins (Sub-cases 7-9 invariants preserved: 1 RenderNode
        // produced when params.animators.empty()).
        (void)chronon3d::registry::internal::wire_through_resolver(lb, "minimal_white", spec);
    };
    d.animator_factory = compose_minimal_white;
    return d;
}

// ── 20. yellow_keyword ────────────────────────────────────────────────────
TextPresetDescriptor yellow_keyword_entry() {
    PresetMetadata meta;
    meta.id           = "yellow_keyword";
    meta.display_name = "YellowKeyword";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "Per-word stagger (Frame{3}) + fade_in.  Pair with a "
                         "yellow appearance.color (caller-set) for karaoke-style "
                         "keyword highlighting.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_yellow_keyword";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "yellow_keyword", spec)
          .word_stagger(Frame{3}, Frame{20})
          .fade_in(Frame{12});
    };
    d.animator_factory = compose_yellow_keyword;
    return d;
}

// ── 21. glow_pulse ────────────────────────────────────────────────────────
TextPresetDescriptor glow_pulse_entry() {
    PresetMetadata meta;
    meta.id           = "glow_pulse";
    meta.display_name = "GlowPulse";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "Continuous tracking-breathing idle (Frame{40}) — "
                         "subtle on-screen presence.  Pair with a glow "
                         "appearance.material (caller-set).";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_glow_pulse";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "glow_pulse", spec)
          .tracking_breathing(0.08f, Frame{40});
    };
    d.animator_factory = compose_glow_pulse;
    return d;
}

// ── 22. caption_box ───────────────────────────────────────────────────────
TextPresetDescriptor caption_box_entry() {
    PresetMetadata meta;
    meta.id           = "caption_box";
    meta.display_name = "CaptionBox";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "Short downward dock (offset.y=30) + fade_in. "
                         "Pixel-accurate caption-box entry.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_caption_box";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "caption_box", spec)
          .fade_in(Frame{12});
    };
    d.animator_factory = compose_caption_box;
    return d;
}


// ── 23. karaoke_fill ────────────────────────────────────────────────────
TextPresetDescriptor karaoke_fill_entry() {
    PresetMetadata meta;
    meta.id           = "karaoke_fill";
    meta.display_name = "KaraokeFill";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "Word-level karaoke fill sweep.";
    meta.builtin      = true;
    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_karaoke_fill";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb, LayerBuilderT& lb, const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "karaoke_fill", spec).word_stagger(Frame{2}, Frame{12}).fade_in(Frame{10});
    };
    d.animator_factory = compose_karaoke_fill;
    return d;
}

// ── 24. active_word_pop ──────────────────────────────────────────────────
TextPresetDescriptor active_word_pop_entry() {
    PresetMetadata meta;
    meta.id           = "active_word_pop";
    meta.display_name = "ActiveWordPop";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "Per-word scale pop driven by subtitle word timing.";
    meta.builtin      = true;
    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_active_word_pop";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb, LayerBuilderT& lb, const TextSpecT& spec) {
        // The per-word scale punch comes from the TextAnimatorSpec's
        // ScaleProperty applied through the word selectors emitted by
        // SubtitleTrackBuilder.  No whole-layer scale transform is used
        // so the highlight is strictly constrained to the active word.
        chronon3d::registry::internal::wire_through_resolver(lb, "active_word_pop", spec).fade_in(Frame{10});
    };
    d.animator_factory = compose_active_word_pop;
    return d;
}

// ── 25. subtitle_card ────────────────────────────────────────────────────
TextPresetDescriptor subtitle_card_entry() {
    PresetMetadata meta;
    meta.id           = "subtitle_card";
    meta.display_name = "SubtitleCard";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "Rounded background card + fade_in.";
    meta.builtin      = true;
    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_subtitle_card";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb, LayerBuilderT& lb, const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "subtitle_card", spec).fade_in(Frame{12});
    };
    d.animator_factory = compose_subtitle_card;
    return d;
}

// ── 26. lower_third_safe ─────────────────────────────────────────────────
TextPresetDescriptor lower_third_safe_entry() {
    PresetMetadata meta;
    meta.id           = "lower_third_safe";
    meta.display_name = "LowerThirdSafe";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "Lower-third dock with safe-area margin.";
    meta.builtin      = true;
    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_lower_third_safe";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb, LayerBuilderT& lb, const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "lower_third_safe", spec).fade_in(Frame{12});
    };
    d.animator_factory = compose_lower_third_safe;
    return d;
}


// ── public factory surface ────────────────────────────────────────────────
//
// `create_text_presets()` returns the 4 Subtitle-category descriptors in
// the canonical Cinematic → Reveal → Emphasis → Subtitle insertion order
// expected by `register_text_preset_subtitle(...)` (Subtitle is the LAST
// category in the umbrella's seed order).  This function does NOT call
// `register_preset` — it is the factory-side half of the canonical
// register pipeline.
//
// The accompanying registration bridge
// `register_helpers_internal::register_text_preset_subtitle` (in
// src/registry/text_preset_registry.cpp) consumes this vector and forwards
// it to `r.register_preset(...)` calls — preserving the umbrella's seed
// order invariant (no duplicate ids).
[[nodiscard]] std::vector<TextPresetDescriptor>
create_text_presets() {
    return {
        minimal_white_entry(),
        yellow_keyword_entry(),
        glow_pulse_entry(),
        caption_box_entry(),
        karaoke_fill_entry(),
        active_word_pop_entry(),
        subtitle_card_entry(),
        lower_third_safe_entry(),
    };
}

} // namespace chronon3d::registry::register_helpers_internal::factory_basic
