// ═══════════════════════════════════════════════════════════════════════════
// tests/helpers/chronon_glow_final.cpp
//
// Provenance: restored to core from the externalized content pack
// (content/compositions/chronon_glow_final.cpp, removed in 6e6905116).
// Pairs with tests/helpers/chronon_glow_final.hpp.
// ═══════════════════════════════════════════════════════════════════════════

#include "chronon_glow_final.hpp"
// Header brings: kDefaultGlowStrength, GlowFormat, ChrononGlowProps, and
// 3 public-function decls (default_landscape_props, default_portrait_props,
// make_chronon_glow_final).  Public surface contract.

#include "cinematic_glow.hpp"

#include <cstddef>
#include <string>

#include <chronon3d/core/types/frame.hpp>
// chronon3d::f32, chronon3d::FrameRate, etc. — used by the factory body.

#include <chronon3d/core/types/frame_context.hpp>
// chronon3d::FrameContext — captured-by-value in the composition lambda.

#include <chronon3d/animation/core/animated_value.hpp>

#include <chronon3d/scene/builders/builder_params.hpp>
// TextDefinition / TextRunDefinition / TextPlacementKind::CanvasCenter /
// TextAlign / VerticalAlign / Vec2 / Vec3 / Color — used by the inner
// scene composer (build_chronon_glow_scene below).

#include <chronon3d/scene/builders/scene_builder.hpp>
// chronon3d::SceneBuilder — the composer is built around it.

#include <chronon3d/scene/builders/layer_builder.hpp>
// chronon3d::LayerBuilder — the inner lambda receives one of these.

#include <chronon3d/text/text_run_shape.hpp>
// chronon3d::TextRunDefinition — used in the composer below.

#include <chronon3d/timeline/composition.hpp>
// chronon3d::composition / chronon3d::Composition / Scene — return type
// of the factory and the lambda return type used by composition(...).

#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/prepared_text.hpp>

namespace {

using chronon3d::content::glow_final::ChrononGlowProps;
using chronon3d::content::glow_final::GlowFormat;

// ── Spec numeric constants (Phase 2 §spec — keep aligned, TU-local) ─────
constexpr chronon3d::f32 kMidOverInnerRatio =
    chronon3d::f32{0.22f} / chronon3d::f32{0.55f};   // ≈ 0.40
constexpr chronon3d::f32 kBloomOverInnerRatio =
    chronon3d::f32{0.08f} / chronon3d::f32{0.55f};   // ≈ 0.14545
constexpr chronon3d::f32 kGlowInnerRadiusPx =
    chronon3d::f32{4.0f};
constexpr chronon3d::f32 kGlowMidRadiusPx =
    chronon3d::f32{14.0f};
constexpr chronon3d::f32 kGlowBloomRadiusPx =
    chronon3d::f32{34.0f};

// ── Layout resolver (Step 8 §B: single source of truth) ────────────────
struct GlowLayout {
    chronon3d::Vec2 canvas_size;
    chronon3d::f32  font_size;
    chronon3d::Vec2 box;
};

GlowLayout resolve_layout(GlowFormat format) {
    switch (format) {
        case GlowFormat::Landscape:
            return GlowLayout{
                chronon3d::Vec2{1920.0f, 1080.0f},
                chronon3d::f32{230.0f},
                chronon3d::Vec2{1700.0f, 360.0f},
            };
        case GlowFormat::Portrait:
            return GlowLayout{
                chronon3d::Vec2{1080.0f, 1920.0f},
                chronon3d::f32{160.0f},
                chronon3d::Vec2{1000.0f, 280.0f},
            };
    }
    return GlowLayout{};  // unreachable (suppress -Wreturn-type)
}

// ── Cinematic glow preset helper (Phase 2 §spec, TU-local) ────────────
chronon3d::content::text_reveal::CinematicGlowPreset
default_cinematic_preset(float strength) {
    return chronon3d::content::text_reveal::CinematicGlowPreset{
        .inner_radius    = kGlowInnerRadiusPx,
        .mid_radius      = kGlowMidRadiusPx,
        .bloom_radius    = kGlowBloomRadiusPx,
        .inner_intensity = strength,
        .mid_intensity   = strength * kMidOverInnerRatio,
        .bloom_intensity = strength * kBloomOverInnerRatio,
        .micro_shadow    = true,
    };
}

// ── Continuous envelope (preserves the 0/15/30 reference values) ─────────
const chronon3d::AnimatedValue<chronon3d::f32>& opacity_animation() {
    static const chronon3d::AnimatedValue<chronon3d::f32> track{
        {{0, chronon3d::f32{0.40f}},
         {15, chronon3d::f32{0.85f}},
         {30, chronon3d::f32{0.50f}}}};
    return track;
}

const chronon3d::AnimatedValue<chronon3d::Vec3>& scale_animation() {
    static const chronon3d::AnimatedValue<chronon3d::Vec3> track{
        {{0, chronon3d::Vec3{0.96f, 0.96f, 1.0f}},
         {15, chronon3d::Vec3{1.05f, 1.05f, 1.0f}},
         {30, chronon3d::Vec3{0.98f, 0.98f, 1.0f}}}};
    return track;
}

// ── Inner scene composer (TU-local; shared between factory + future detours) ──
void build_chronon_glow_scene(
        chronon3d::SceneBuilder& s,
        const ChrononGlowProps& props,
        const chronon3d::SampleTime& sample_time) {
    // Step 8 §B: single source of truth for layout (derived from format).
    const GlowLayout layout = resolve_layout(props.format);
    const chronon3d::f32 opacity = opacity_animation().evaluate(sample_time);
    const chronon3d::Vec3 scale  = scale_animation().evaluate(sample_time);
    const bool apply_breath = props.scale_breath;

    s.layer("hero", [&, opacity, scale, apply_breath](chronon3d::LayerBuilder& l) {
        l.text("glow_pulse", chronon3d::TextDefinition{
            .content = {.value = props.text},
            .style = {
                .font = {
                    .font_path   = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 700,
                    .font_size   = layout.font_size,
                },
                .color = chronon3d::Color::white()
            },
            .frame = {
                .size = layout.box,
                .placement = chronon3d::TextPlacement{
                    chronon3d::TextPlacementKind::CanvasCenter,
                    {},
                },
                .align = chronon3d::TextAlign::Center,
                .vertical_align = chronon3d::VerticalAlign::Middle
            }
        });
        // Per-frame envelope: opacity always; scale gated by the
        // scale_breath flag (Phase 3 SCALA fix means non-identity scale
        // does not break the canvas-center bake).
        l.opacity(opacity);
        if (apply_breath) {
            l.scale(scale);
        }

        // Phase 2 cinematic glow (optional, gated by props.glow_enabled).
        // A zero-strength enabled glow is the authoring equivalent of the
        // disabled path: do not enqueue zero-opacity glow/shadow passes.
        if (props.glow_enabled && props.glow_strength > 0.0f) {
            chronon3d::content::text_reveal::apply_cinematic_glow(
                l, default_cinematic_preset(props.glow_strength));
        }
    });
}

} // namespace (TU-local helpers)

// ═══════════════════════════════════════════════════════════════════════════
// Public surface — out-of-line definitions matching the 3 decls in
// tests/helpers/chronon_glow_final.hpp.
// ═══════════════════════════════════════════════════════════════════════════

namespace chronon3d::content::glow_final {

ChrononGlowProps default_landscape_props() {
    return ChrononGlowProps{
        .text          = "PULSE GLOW",
        .format        = GlowFormat::Landscape,
        .glow_enabled  = true,
        .glow_strength = kDefaultGlowStrength,
        .scale_breath  = true,  // Step 8 §B: new default (Phase 3 SCALA safe)
    };
}

ChrononGlowProps default_portrait_props() {
    return ChrononGlowProps{
        .text          = "PULSE GLOW",
        .format        = GlowFormat::Portrait,
        .glow_enabled  = true,
        .glow_strength = kDefaultGlowStrength,
        .scale_breath  = true,  // Step 8 §B: new default (Phase 3 SCALA safe)
    };
}

chronon3d::Composition make_chronon_glow_final(ChrononGlowProps props) {
    const GlowLayout layout = resolve_layout(props.format);
    const std::string name = (props.format == GlowFormat::Portrait)
        ? std::string{"ChrononGlowFinal/9x16"}
        : std::string{"ChrononGlowFinal/16x9"};
    const chronon3d::i32 w = static_cast<chronon3d::i32>(layout.canvas_size.x);
    const chronon3d::i32 h = static_cast<chronon3d::i32>(layout.canvas_size.y);
    return chronon3d::composition(
        {
            .name       = name,
            .width      = w,
            .height     = h,
            .frame_rate = chronon3d::FrameRate{30, 1},
            .duration   = 60,
        },
        [props](const chronon3d::FrameContext& ctx) -> chronon3d::Scene {
            chronon3d::SceneBuilder s(ctx);
            // The canonical runtime owns the font resolver.  Attach its
            // FontEngine to the scene so Glow V1 never falls back to the
            // process-wide resolver when rendered by SDK/tests.
            if (ctx.font_engine) {
                s.font_engine(ctx.font_engine);
            }
            build_chronon_glow_scene(
                s, props, ctx.local_time());
            return s.build();
        });
}

} // namespace chronon3d::content::glow_final
