// ─── src/registry/text_preset_factories_emphasis.cpp ─────────────────────
//
// FASE 1 Step 3 — Emphasis-category text-preset factory TU.
// Extracted from the anon namespace of src/registry/text_preset_registry.cpp.
//
// ## Content (verbatim port)
//
//   ── EMPHASIS (4) ─────────────────────────────────────────────────────────
//     1. word_pop                   (Stage 3, FASE 2b Word selector)
//     2. scale_punch                (Stage 3)
//     3. color_accent               (Stage 3)
//     4. gradient_fill              (Stage 3)
//
// ## Architectural invariants (AGENTS.md v0.1 freeze)
//
//  (1) SINGLE central registry.
//  (2) Factory surface is READ-ONLY descriptors.
//  (3) Uses `chronon3d::registry::internal::make_presetc_template` and
//      `chronon3d::registry::internal::wire_through_resolver`.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/registry/text_preset_descriptor.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>

#include "text_preset_internal_helpers.hpp"

#include <optional>
#include <string>
#include <vector>

namespace chronon3d::registry::register_helpers_internal::factory_emphasis {

using LayerBuilderT  = chronon3d::registry::internal::LayerBuilderT;
using SceneBuilderT  = chronon3d::registry::internal::SceneBuilderT;
using TextSpecT      = chronon3d::registry::internal::TextSpecT;

// ── STAGE 2 helpers — Emphasis (4) compositor bodies (verbatim port) ──────

// 1. word_pop — per-word bouncy scale overshoot with Word selector.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_word_pop(const PresetMetadata& /*meta*/) {
    const EasingCurve eo_pop{Easing::OutBack};
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("word_pop");
    a.properties.push_back(ScaleProperty{Vec3{1.15f, 1.15f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});

    a.selectors.clear();
    GlyphSelectorSpec word_sel;
    word_sel.id     = a.id + "_sel_word";
    word_sel.unit   = TextSelectorUnit::Word;
    word_sel.shape  = TextSelectorShape::Square;
    word_sel.start  = AnimatedValue<f32>{0.0f};
    word_sel.end    = AnimatedValue<f32>{
        {{Frame{0},  0.0f,  eo_pop},
         {Frame{20}, 100.0f, eo_pop}}};
    word_sel.amount = AnimatedValue<f32>{100.0f};
    a.selectors.push_back(word_sel);

    return a;
}

// 2. scale_punch — scale_drop(0.70,12) + soft_pop(20)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_scale_punch(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("scale_punch");
    a.properties.push_back(ScaleProperty{Vec3{0.70f, 0.70f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 3. color_accent — fade_in(12)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_color_accent(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("color_accent");
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 4. gradient_fill — fade_shift_vertical({0,80,0},20) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_gradient_fill(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = chronon3d::registry::internal::make_presetc_template("gradient_fill");
    a.properties.push_back(PositionProperty{Vec3{0.0f, 80.0f, 0.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}


// ── 1. word_pop ──────────────────────────────────────────────────────────
TextPresetDescriptor word_pop_entry() {
    PresetMetadata meta;
    meta.id           = "word_pop";
    meta.display_name = "WordPop";
    meta.category     = TextPresetCategory::Emphasis;
    meta.description  = "Bouncy scale overshoot (1.0 → 1.15) + soft_pop.  "
                         "FASE 2b: now uses a Word selector with animated end sweep "
                         "(0→100 over 20f OutBack) so the pop effect targets each word "
                         "independently in sequence — per-word emphasis pop, "
                         "best in mid-flow body copy.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/emphasis_word_pop";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        (void)chronon3d::registry::internal::wire_through_resolver(lb, "word_pop", spec);
    };
    d.animator_factory = compose_word_pop;
    return d;
}

// ── 2. scale_punch ───────────────────────────────────────────────────────
TextPresetDescriptor scale_punch_entry() {
    PresetMetadata meta;
    meta.id           = "scale_punch";
    meta.display_name = "ScalePunch";
    meta.category     = TextPresetCategory::Emphasis;
    meta.description  = "Compression scale_drop (1.0 → 0.7) + soft_pop "
                         "(snap-back).  Impact-punch emphasis.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/emphasis_scale_punch";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "scale_punch", spec)
          .scale_drop(0.70f, Frame{12})
          .soft_pop(Frame{20});
    };
    d.animator_factory = compose_scale_punch;
    return d;
}

// ── 3. color_accent ──────────────────────────────────────────────────────
TextPresetDescriptor color_accent_entry() {
    PresetMetadata meta;
    meta.id           = "color_accent";
    meta.display_name = "ColorAccent";
    meta.category     = TextPresetCategory::Emphasis;
    meta.description  = "Pure fade_in; colour comes from spec.appearance.color "
                         "(set by the caller / pipeline).  Pipe colour-accent "
                         "without touching motion.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/emphasis_color_accent";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "color_accent", spec)
          .fade_in(Frame{12});
    };
    d.animator_factory = compose_color_accent;
    return d;
}

// ── 4. gradient_fill ─────────────────────────────────────────────────────
TextPresetDescriptor gradient_fill_entry() {
    PresetMetadata meta;
    meta.id           = "gradient_fill";
    meta.display_name = "GradientFill";
    meta.category     = TextPresetCategory::Emphasis;
    meta.description  = "Short downward slide + fade_in; gradient paint "
                         "comes from spec.appearance.paint (caller-set).";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.fixture         = "tests/visual/text/emphasis_gradient_fill";
    d.metadata        = meta;
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        chronon3d::registry::internal::wire_through_resolver(lb, "gradient_fill", spec)
          .fade_shift_vertical(Vec3{0.0f, 80.0f, 0.0f}, Frame{20})
          .fade_in(Frame{15});
    };
    d.animator_factory = compose_gradient_fill;
    return d;
}


// ── public factory surface ────────────────────────────────────────────────
[[nodiscard]] std::vector<TextPresetDescriptor>
create_text_presets() {
    return {
        word_pop_entry(),
        scale_punch_entry(),
        color_accent_entry(),
        gradient_fill_entry(),
    };
}

} // namespace chronon3d::registry::register_helpers_internal::factory_emphasis
