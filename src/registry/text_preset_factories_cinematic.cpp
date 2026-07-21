// ─── src/registry/text_preset_factories_cinematic.cpp ─────────────────────
//
// FASE 1 Step 2 — Cinematic-category text-preset factory TU.
// Extracted from the anon namespace of src/registry/text_preset_registry.cpp.
//
// ## Content (verbatim port)
//
//   ── CINEMATIC (4) ────────────────────────────────────────────────────────
//     1. animation_compositions     (PR 41cda40c, kept)
//     2. cinematic_text_camera      (PR 41cda40c, kept)
//     3. cinematic_title_reveal     (PR 41cda40c, AGENT 2 animated timeline)
//     4. tilt_sweep_title_v2        (PR 41cda40c, kept)
//
// ## Architectural invariants (AGENTS.md v0.1 freeze)
//
//  (1) SINGLE central registry.
//  (2) Factory surface is READ-ONLY descriptors.
//  (3) Uses `::chronon3d::registry::internal::make_presetc_template` and
//      `::chronon3d::registry::internal::wire_through_resolver`.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/registry/text_preset_descriptor.hpp>
#include <chronon3d/registry/animator_resolver.hpp>

#include <chronon3d/scene/builders/builder_params.hpp>

#include "text_preset_internal_helpers.hpp"

#include <optional>
#include <string>
#include <vector>

namespace chronon3d::registry::register_helpers_internal::factory_cinematic {

using LayerBuilderT  = ::chronon3d::registry::internal::LayerBuilderT;
using SceneBuilderT  = ::chronon3d::registry::internal::SceneBuilderT;
using TextSpecT      = ::chronon3d::registry::internal::TextSpecT;

// ── STAGE 2 helpers — Cinematic (4) compositor bodies (verbatim port) ─────

// 1. animation_compositions — depth_reveal(280,45) + soft_pop(30) + float_idle(8,120)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_animation_compositions(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = ::chronon3d::registry::internal::make_presetc_template("animation_compositions");
    a.properties.push_back(PositionProperty{Vec3{0.0f, 8.0f, 280.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 2. cinematic_text_camera — depth_reveal(260,50) + scale_drop(0.95,30) + soft_pop(24)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_cinematic_text_camera(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = ::chronon3d::registry::internal::make_presetc_template("cinematic_text_camera");
    a.properties.push_back(PositionProperty{Vec3{0.0f, 0.0f, 260.0f}});
    a.properties.push_back(ScaleProperty{Vec3{0.95f, 0.95f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 3. cinematic_title_reveal — animated timeline (ease-out cubic, 36f).
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_cinematic_title_reveal(const PresetMetadata& /*meta*/) {
    const EasingCurve eo_cine{Easing::OutCubic};
    TextAnimatorSpec a = ::chronon3d::registry::internal::make_presetc_template("cinematic_title_reveal");

    ScaleProperty sp;
    sp.value.add_keyframe(Frame{0},  Vec3{0.92f, 0.92f, 1.0f}, eo_cine);
    sp.value.add_keyframe(Frame{36}, Vec3{1.0f,  1.0f,  1.0f}, eo_cine);
    a.properties.push_back(sp);

    OpacityProperty op;
    op.value.add_keyframe(Frame{0},  0.0f, eo_cine);
    op.value.add_keyframe(Frame{36}, 1.0f, eo_cine);
    a.properties.push_back(op);

    BlurProperty bp;
    bp.radius.add_keyframe(Frame{0},  12.0f, eo_cine);
    bp.radius.add_keyframe(Frame{36}, 0.0f,  eo_cine);
    a.properties.push_back(bp);

    PositionProperty pp;
    pp.value.add_keyframe(Frame{0},  Vec3{0.0f, 40.0f, 0.0f}, eo_cine);
    pp.value.add_keyframe(Frame{36}, Vec3{0.0f, 0.0f,  0.0f}, eo_cine);
    a.properties.push_back(pp);

    return a;
}

// 4. tilt_sweep_title_v2 — scale_drop(1.08,45) + focus_in(2.5,30) + soft_pop(24)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_tilt_sweep_title_v2(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = ::chronon3d::registry::internal::make_presetc_template("tilt_sweep_title_v2");
    a.properties.push_back(ScaleProperty{Vec3{1.08f, 1.08f, 1.0f}});
    a.properties.push_back(BlurProperty{2.5f});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}


// ── 1. animation_compositions ────────────────────────────────────────────
TextPresetDescriptor animation_compositions_entry() {
    PresetMetadata meta;
    meta.id           = "animation_compositions";
    meta.display_name = "Animation compositions utility suite";
    meta.category     = TextPresetCategory::Cinematic;
    meta.description  = "Catalogues helper functions for animation compositions "
                         "(reveal/tilt/word-shimmer factory). Cinematic depth-reveal "
                         "+ soft-pop + float_idle motion preset.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/cinematic_motion/DeepParallaxCascade";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        ::chronon3d::registry::internal::wire_through_resolver(lb, "animation_compositions", spec)
          .depth_reveal(280.0f, Frame{45})
          .soft_pop(Frame{30})
          .float_idle(8.0f, Frame{120});
    };
    d.animator_factory = compose_animation_compositions;
    return d;
}

// ── 2. cinematic_text_camera ─────────────────────────────────────────────
TextPresetDescriptor cinematic_text_camera_entry() {
    PresetMetadata meta;
    meta.id           = "cinematic_text_camera";
    meta.display_name = "Cinematic text-camera compositions (5 hero comps)";
    meta.category     = TextPresetCategory::Cinematic;
    meta.description  = "5 hero cinematic compositions (DeepParallaxCascade, "
                         "WhipPanHeroReveal, OrbitHandheldGlow, RackFocusTitleSwap, "
                         "AbyssFreefallStagger). Camera-driven depth reveal. "
                         "Stage 4 (TICKET-A2): resolver-wires a TextAnimatorSpec "
                         "onto the TextRun when the spec carries rich-paint "
                         "signals (stroke_enabled / fill_style / stroke_style).";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/camera/camera_visual_tests";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        TextRunSpec params;
        params.text = spec;
        if (::chronon3d::registry::AnimatorResolver::spec_is_rich(spec)) {
            params.animators.push_back(
                ::chronon3d::registry::AnimatorResolver::rich_paint_anchor("cinematic_text_camera"));
        }
        if (auto canonical = ::chronon3d::registry::AnimatorResolver::compose_for("cinematic_text_camera")) {
            params.animators.push_back(*canonical);
        }
        lb.text_run("camera_text", params)
          .commit()
          .depth_reveal(260.0f, Frame{50})
          .scale_drop(0.95f, Frame{30})
          .soft_pop(Frame{24});
    };
    d.animator_factory = compose_cinematic_text_camera;
    return d;
}

// ── 3. cinematic_title_reveal ────────────────────────────────────────────
TextPresetDescriptor cinematic_title_reveal_entry() {
    PresetMetadata meta;
    meta.id           = "cinematic_title_reveal";
    meta.display_name = "Cinematic title reveal (push-in/tilt variants)";
    meta.category     = TextPresetCategory::Cinematic;
    meta.description  = "Cinematic title reveal utilities — push-in + tilt "
                         "variants for hero/section titles with subtle drift. "
                         "AGENT 2 (TICKET-A2) — animation is resolver-driven "
                         "(Scale/Opacity/Blur/Position AnimatedValue over 36f "
                         "ease-out cubic); no layer-level chain so the canonical "
                         "resolver path is the single source of keyframes.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/cinematic_motion/cinematic_title_reveal";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        (void)::chronon3d::registry::internal::wire_through_resolver(lb, "cinematic_title_reveal", spec);
    };
    d.animator_factory = compose_cinematic_title_reveal;
    return d;
}

// ── 4. tilt_sweep_title_v2 ───────────────────────────────────────────────
TextPresetDescriptor tilt_sweep_title_v2_entry() {
    PresetMetadata meta;
    meta.id           = "tilt_sweep_title_v2";
    meta.display_name = "Tilt-sweep title v2";
    meta.category     = TextPresetCategory::Cinematic;
    meta.description  = "Tilt-sweep title with cinematic push-in reveal, "
                         "scale animation, and blur ramp — cross-link "
                         "tilt_sweep_title preset family.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/cinematic_motion/tilt_sweep_title_v2";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        ::chronon3d::registry::internal::wire_through_resolver(lb, "tilt_sweep_title_v2", spec)
          .scale_drop(1.08f, Frame{45})
          .focus_in(2.5f, Frame{30})
          .soft_pop(Frame{24});
    };
    d.animator_factory = compose_tilt_sweep_title_v2;
    return d;
}


// ── public factory surface ────────────────────────────────────────────────
[[nodiscard]] std::vector<TextPresetDescriptor>
create_text_presets() {
    return {
        animation_compositions_entry(),
        cinematic_text_camera_entry(),
        cinematic_title_reveal_entry(),
        tilt_sweep_title_v2_entry(),
    };
}

} // namespace chronon3d::registry::register_helpers_internal::factory_cinematic
