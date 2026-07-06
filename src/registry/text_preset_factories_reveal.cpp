// ─── src/registry/text_preset_factories_reveal.cpp ──────────────────────────
//
// FASE 1 Step 1 — Reveal-category text-preset factory TU.
// Extracted from the anon namespace of src/registry/text_preset_registry.cpp.
//
// ## Content (verbatim port)
//
//   ── REVEAL (10) ──────────────────────────────────────────────────────────
//     1.  text_animations            (PR 41cda40c, kept)
//     2.  fade_in                    (Stage 3)
//     3.  blur_in                    (Stage 3)
//     4.  slide_up                   (Stage 3)
//     5.  slide_down                 (Stage 3)
//     6.  scale_in                   (Stage 3)
//     7.  tracking_close             (Stage 3, AGENT 2 animated timeline)
//     8.  masked_line_reveal         (Stage 3, FASE 2b Line selector)
//     9.  word_cascade               (Stage 3, AGENT 2 Word selector)
//     10. character_cascade          (Stage 3, FASE 2b Grapheme selector)
//
// ## Architectural invariants (AGENTS.md v0.1 freeze)
//
//  (1) SINGLE central registry.  AnimatorResolver queries ONLY
//      `chronon3d::registry::builtin_text_preset_registry()`.
//
//  (2) Factory surface is READ-ONLY descriptors.  This file returns
//      `std::vector<TextPresetDescriptor>` from `create_text_presets()`;
//      it does NOT call `TextPresetRegistry::register_preset`.
//
//  (3) Uses `chronon3d::registry::internal::make_presetc_template` and
//      `chronon3d::registry::internal::wire_through_resolver` from
//      the shared internal helper header.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/registry/text_preset_descriptor.hpp>
// PresetMetadata, TextPresetDescriptor, TextPresetCategory, AnimatorFactory.

#include <chronon3d/scene/builders/builder_params.hpp>
// canonical ::chronon3d::TextSpec, TextAnimatorSpec, AnimatedValue<>,
// EasingCurve, Easing, Frame, GlyphSelectorSpec, TextSelectorUnit,
// TextSelectorShape, TextPropertyBlendMode.

#include "text_preset_internal_helpers.hpp"
// internal::make_presetc_template + internal::wire_through_resolver
// + LayerBuilderT / SceneBuilderT / TextSpecT aliases.

#include <optional>
#include <string>
#include <vector>

namespace chronon3d::registry::register_helpers_internal::factory_reveal {

using LayerBuilderT  = chronon3d::registry::internal::LayerBuilderT;
using SceneBuilderT  = chronon3d::registry::internal::SceneBuilderT;
using TextSpecT      = chronon3d::registry::internal::TextSpecT;

// ── STAGE 2 helpers — Reveal (10) compositor bodies (verbatim port) ────────
//
// Each helper mirrors the corresponding pre-TEXT-RES-01
// `AnimatorResolver::compose_for(...)` branched body verbatim.  Naming
// pattern preserved byte-identical to the pre-extraction anon namespace.

// 1. text_animations — fade_in(20) + scale_drop(0.95,30)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_text_animations(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("text_animations");
    a.properties.push_back(ScaleProperty{Vec3{0.95f, 0.95f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 2. fade_in — fade_in(15) + soft_pop(10)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_fade_in(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("fade_in");
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 3. blur_in — focus_in(4.0,30) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_blur_in(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("blur_in");
    a.properties.push_back(BlurProperty{4.0f});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 4. slide_up — fade_shift_vertical({0,200,0},25) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_slide_up(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("slide_up");
    a.properties.push_back(PositionProperty{Vec3{0.0f, 200.0f, 0.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 5. slide_down — fade_shift_vertical({0,-200,0},25) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_slide_down(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("slide_down");
    a.properties.push_back(PositionProperty{Vec3{0.0f, -200.0f, 0.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 6. scale_in — scale_drop(0.85,25) + soft_pop(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_scale_in(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("scale_in");
    a.properties.push_back(ScaleProperty{Vec3{0.85f, 0.85f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 7. tracking_close — AGENT 2 — 35f timeline, tracking 0.18→0 (OutExpo) + opacity 0→1.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_tracking_close(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("tracking_close");

    TrackingProperty tp;
    tp.pixels.add_keyframe(Frame{0},  0.18f, EasingCurve{Easing::OutExpo});
    tp.pixels.add_keyframe(Frame{35}, 0.0f,  EasingCurve{Easing::Linear});
    a.properties.push_back(tp);

    OpacityProperty op;
    op.value.add_keyframe(Frame{0},  0.0f, EasingCurve{Easing::Linear});
    op.value.add_keyframe(Frame{35}, 1.0f, EasingCurve{Easing::Linear});
    a.properties.push_back(op);

    return a;
}

// 8. masked_line_reveal — FASE 2b — per-line reveal with Line selector.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_masked_line_reveal(const PresetMetadata& /*meta*/) {
    const EasingCurve eo_line{Easing::OutCubic};
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("masked_line_reveal");
    a.properties.push_back(PositionProperty{Vec3{120.0f, 0.0f, 0.0f}});

    // FASE 2b: Override default global Glyph selector with per-Line selector.
    a.selectors.clear();
    GlyphSelectorSpec line_sel;
    line_sel.id     = a.id + "_sel_line";
    line_sel.unit   = TextSelectorUnit::Line;
    line_sel.shape  = TextSelectorShape::Square;
    line_sel.start  = AnimatedValue<f32>{0.0f};
    line_sel.end    = AnimatedValue<f32>{
        {{Frame{0},  0.0f,  eo_line},
         {Frame{30}, 100.0f, eo_line}}};
    line_sel.amount = AnimatedValue<f32>{100.0f};
    a.selectors.push_back(line_sel);

    return a;
}

// 9. word_cascade — AGENT 2 — per-word cascade (Word selector + animated end).
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_word_cascade(const PresetMetadata& /*meta*/) {
    const EasingCurve eo_words{Easing::OutCubic};
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("word_cascade");

    OpacityProperty op;
    op.value.add_keyframe(Frame{0},  0.0f,  eo_words);
    op.value.add_keyframe(Frame{36}, 1.0f,  eo_words);
    a.properties.push_back(op);

    PositionProperty pp;
    pp.value.add_keyframe(Frame{0},  Vec3{0.0f, 30.0f, 0.0f}, eo_words);
    pp.value.add_keyframe(Frame{36}, Vec3{0.0f, 0.0f,  0.0f}, eo_words);
    a.properties.push_back(pp);

    ScaleProperty sp;
    sp.value.add_keyframe(Frame{0},  Vec3{0.96f, 0.96f, 1.0f}, eo_words);
    sp.value.add_keyframe(Frame{36}, Vec3{1.0f,  1.0f,  1.0f}, eo_words);
    a.properties.push_back(sp);

    // Override default global selector: Word unit + animated `end` (0→100 over 48f).
    a.selectors.clear();
    GlyphSelectorSpec word_sel;
    word_sel.id     = a.id + "_sel_word";
    word_sel.unit   = TextSelectorUnit::Word;
    word_sel.shape  = TextSelectorShape::Square;
    word_sel.start  = AnimatedValue<f32>{0.0f};
    word_sel.end    = AnimatedValue<f32>{
        {{Frame{0},  0.0f,   eo_words},
         {Frame{48}, 100.0f, eo_words}}};
    word_sel.amount = AnimatedValue<f32>{100.0f};
    a.selectors.push_back(word_sel);

    return a;
}

// 10. character_cascade — FASE 2b — per-grapheme cascade (Grapheme selector + animated end).
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_character_cascade(const PresetMetadata& /*meta*/) {
    const EasingCurve eo_char{Easing::OutCubic};
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("character_cascade");

    OpacityProperty op;
    op.value.add_keyframe(Frame{0},  0.0f,  eo_char);
    op.value.add_keyframe(Frame{18}, 1.0f,  eo_char);
    a.properties.push_back(op);

    PositionProperty pp;
    pp.value.add_keyframe(Frame{0},  Vec3{0.0f, 18.0f, 0.0f}, eo_char);
    pp.value.add_keyframe(Frame{18}, Vec3{0.0f, 0.0f,  0.0f}, eo_char);
    a.properties.push_back(pp);

    ScaleProperty sp;
    sp.value.add_keyframe(Frame{0},  Vec3{0.9f, 0.9f, 1.0f}, eo_char);
    sp.value.add_keyframe(Frame{18}, Vec3{1.0f, 1.0f, 1.0f}, eo_char);
    a.properties.push_back(sp);

    // FASE 2b: Grapheme unit instead of Glyph.
    a.selectors.clear();
    GlyphSelectorSpec grapheme_sel;
    grapheme_sel.id     = a.id + "_sel_grapheme";
    grapheme_sel.unit   = TextSelectorUnit::Grapheme;
    grapheme_sel.shape  = TextSelectorShape::Square;
    grapheme_sel.start  = AnimatedValue<f32>{0.0f};
    grapheme_sel.end    = AnimatedValue<f32>{
        {{Frame{0},  0.0f,  eo_char},
         {Frame{24}, 100.0f, eo_char}}};
    grapheme_sel.amount = AnimatedValue<f32>{100.0f};
    a.selectors.push_back(grapheme_sel);

    return a;
}


// ── 1.  text_animations ───────────────────────────────────────────────────
TextPresetDescriptor text_animations_entry() {
    PresetMetadata meta;
    meta.id           = "text_animations";
    meta.display_name = "Text animations utility (typewriter + emphasis)";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Reveal-oriented text animation utilities — typewriter "
                         "+ per-glyph emphasis (word pop, scale punch, gradient fill). "
                         "fade_in + scale_drop entrance.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/PR3/pr3_compositions";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "text_animations", spec)
          .fade_in(Frame{20})
          .scale_drop(0.95f, Frame{30});
    };
    d.animator_factory = compose_text_animations;
    return d;
}

// ── 2.  fade_in ───────────────────────────────────────────────────────────
TextPresetDescriptor fade_in_entry() {
    PresetMetadata meta;
    meta.id           = "fade_in";
    meta.display_name = "FadeIn";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Pure opacity ramp over Frame{15}, no spatial motion. "
                         "Canonical reveal for body copy / subtitle-level text.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_fade_in";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "fade_in", spec)
          .fade_in(Frame{15})
          .soft_pop(Frame{10});
    };
    d.animator_factory = compose_fade_in;
    return d;
}

// ── 3.  blur_in ───────────────────────────────────────────────────────────
TextPresetDescriptor blur_in_entry() {
    PresetMetadata meta;
    meta.id           = "blur_in";
    meta.display_name = "BlurIn";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Focus ramp (4.0 → 0.0 blur) over Frame{30}, paired with "
                         "short fade_in.  Classic motion-graphic reveal pattern.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_blur_in";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "blur_in", spec)
          .focus_in(4.0f, Frame{30})
          .fade_in(Frame{15});
    };
    d.animator_factory = compose_blur_in;
    return d;
}

// ── 4.  slide_up ──────────────────────────────────────────────────────────
TextPresetDescriptor slide_up_entry() {
    PresetMetadata meta;
    meta.id           = "slide_up";
    meta.display_name = "SlideUp";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Vertical slide-up (from below, offset {0,200,0}) + "
                         "fade_in over Frame{25}.  Eyebrow / section-header pattern.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_slide_up";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "slide_up", spec)
          .fade_shift_vertical(Vec3{0.0f, 200.0f, 0.0f}, Frame{25})
          .fade_in(Frame{15});
    };
    d.animator_factory = compose_slide_up;
    return d;
}

// ── 5.  slide_down ────────────────────────────────────────────────────────
TextPresetDescriptor slide_down_entry() {
    PresetMetadata meta;
    meta.id           = "slide_down";
    meta.display_name = "SlideDown";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Vertical slide-down (from above, offset {0,-200,0}) + "
                         "fade_in over Frame{25}.  Bottom-docked subtitle entry.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_slide_down";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "slide_down", spec)
          .fade_shift_vertical(Vec3{0.0f, -200.0f, 0.0f}, Frame{25})
          .fade_in(Frame{15});
    };
    d.animator_factory = compose_slide_down;
    return d;
}

// ── 6.  scale_in ──────────────────────────────────────────────────────────
TextPresetDescriptor scale_in_entry() {
    PresetMetadata meta;
    meta.id           = "scale_in";
    meta.display_name = "ScaleIn";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Scale drop (0.85 → 1.0) over Frame{25} + soft_pop "
                         "settle.  Section-header crop pattern.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_scale_in";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "scale_in", spec)
          .scale_drop(0.85f, Frame{25})
          .soft_pop(Frame{15});
    };
    d.animator_factory = compose_scale_in;
    return d;
}

// ── 7.  tracking_close ────────────────────────────────────────────────────
TextPresetDescriptor tracking_close_entry() {
    PresetMetadata meta;
    meta.id           = "tracking_close";
    meta.display_name = "TrackingClose";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Letter-spacing idle pulse (tracking_breathing) over "
                         "Frame{30}.  Use AFTER a parent cinematic preset; "
                         "no entrance motion of its own. AGENT 2 (TICKET-A2) — "
                         "Tracking 0.18→0 over 35f and Opacity 0→1 are "
                         "resolver-driven (AnimatedValue<>); the factory body "
                         "is just the canonical wire-through-resolver path.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_tracking_close";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        (void)chronon3d::registry::internal::wire_through_resolver(lb, "tracking_close", spec);
    };
    d.animator_factory = compose_tracking_close;
    return d;
}

// ── 8.  masked_line_reveal ────────────────────────────────────────────────
TextPresetDescriptor masked_line_reveal_entry() {
    PresetMetadata meta;
    meta.id           = "masked_line_reveal";
    meta.display_name = "MaskedLineReveal";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Center-split mask reveal with per-Line selector.  "
                         "FASE 2b: Lines reveal in sequence (animated `end` 0→100 "
                         "over 30f OutCubic) + horizontal fade_shift.  "
                         "Mask-and-reveal pattern for multi-line text.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_masked_line";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "masked_line_reveal", spec)
          .center_split(Frame{30})
          .fade_shift_horizontal(Vec3{120.0f, 0.0f, 0.0f}, Frame{25});
    };
    d.animator_factory = compose_masked_line_reveal;
    return d;
}

// ── 9.  word_cascade ──────────────────────────────────────────────────────
TextPresetDescriptor word_cascade_entry() {
    PresetMetadata meta;
    meta.id           = "word_cascade";
    meta.display_name = "WordCascade";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Per-word stagger (Frame{3} delay) + fade_in.  Each "
                         "word enters in succession — the classic kinetic-title "
                         "reveal pattern. AGENT 2 (TICKET-A2) — the Word-unit "
                         "selector carries an AnimatedValue<>`end` that sweeps "
                         "0→100 over 48f, and OpacityProperty/PositionProperty/"
                         "ScaleProperty ramp 0→end_state over 36f ease-out cubic. "
                         "The factory routes through the resolver only — no "
                         "layer-level word_stagger + fade_in chain (single "
                         "canonical path, A2.3).";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_word_cascade";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        (void)chronon3d::registry::internal::wire_through_resolver(lb, "word_cascade", spec);
    };
    d.animator_factory = compose_word_cascade;
    return d;
}

// ── 10. character_cascade ─────────────────────────────────────────────────
TextPresetDescriptor character_cascade_entry() {
    PresetMetadata meta;
    meta.id           = "character_cascade";
    meta.display_name = "CharacterCascade";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Per-grapheme stagger (Frame{1} delay) + fade_in.  "
                         "Tighter than WordCascade — best on monospace / kerned display. "
                         "FASE 2b: Grapheme-unit selector (was Glyph) so combining marks, "
                         "ZWJ emoji, and accented characters animate as a single visual unit. "
                         "AnimatedValue<>`end` (24f) + property ramps over 18f "
                         "ease-out cubic; factory routes through the resolver "
                         "only — no layer-level fade_in + word_stagger chain "
                         "(single canonical path, A2.3).";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_character_cascade";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        (void)chronon3d::registry::internal::wire_through_resolver(lb, "character_cascade", spec);
    };
    d.animator_factory = compose_character_cascade;
    return d;
}


// ── public factory surface ────────────────────────────────────────────────
//
// `create_text_presets()` returns the 10 Reveal-category descriptors in
// the canonical insertion order.
[[nodiscard]] std::vector<TextPresetDescriptor>
create_text_presets() {
    return {
        text_animations_entry(),
        fade_in_entry(),
        blur_in_entry(),
        slide_up_entry(),
        slide_down_entry(),
        scale_in_entry(),
        tracking_close_entry(),
        masked_line_reveal_entry(),
        word_cascade_entry(),
        character_cascade_entry(),
    };
}

} // namespace chronon3d::registry::register_helpers_internal::factory_reveal
