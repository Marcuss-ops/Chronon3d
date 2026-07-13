// ═══════════════════════════════════════════════════════════════════════════
//  whip_pan_hero_reveal.cpp — Phase-2.3 split: 1 composition per file.
//
//  Phase-2.3 mechanical extraction (verbatim) of
//  composition whip_pan_hero_reveal() from the monolithic
//  cinematic_text_camera.cpp (was 667 LOC).  Behaviour preserved
//  bit-identical: 8 "streak" layers + CinematicGlowPreset-driven
//  CHRONON3D stagger reveal via build_text_reveal_line + a
//  MOTION BY CAMERA subtitle snap-in + a hand-rolled whip-pan
//  camera AnimatedValue<Vec3> timeline.
//
//  Source-of-truth factory decl: whip_pan_hero_reveal.hpp
//  (1-line forward decl) reached either directly or through the
//  cinematic_text_camera.hpp umbrella.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <chronon3d/animation/path/catmull_rom_path.hpp>
#include <chronon3d/animation/effects/wiggle.hpp>
#include <chronon3d/animation/motion/timeline.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/text/text_glow_spec.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_definition.hpp>

#include "content/showcases/cinematic/cinematic_showcase_helpers.hpp"
#include "content/showcases/cinematic/cinematic_text_camera.hpp"
#include "content/common/text/text_reveal.hpp"
#include "content/text/text_theme.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace chronon3d::content::anims {

namespace {

// Phase-2.3 review-fix — restore anon-namespace `using` declarations
// that the original monolithic cinematic_text_camera.cpp provided via
// implicit using-directive on its enclosing-namespace leakage.  After
// the per-composition .cpp split, this TU no longer inherits that
// injection; the three names below are referenced unqualified in the
// factory body beyond this point.  Keep scope narrow: only what
// WHIP_PAN_HERO_REVEAL actually uses (the 9 other declarations from
// the original block live in deep_parallax_cascade / orbit_ / rack_
// focus / abyss / and are not needed here).
using chronon3d::content::text_reveal::TextRevealDescriptor;
using chronon3d::content::text_reveal::build_text_reveal_line;
using chronon3d::content::text_reveal::font_bold;

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// 2. WhipPanHeroReveal
//    Camera translates violently left→right (overshoot) and snaps onto
//    the hero. As it locks, a stagger typewriter lifts the title char
//    by char with an OutBounce snap.
// ═══════════════════════════════════════════════════════════════════════════
Composition whip_pan_hero_reveal() {
    return composition({
        .name     = "WhipPanHeroReveal",
        .width    = 1920,
        .height   = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // codex/agent2-font-bind-fixes — see deep_parallax_cascade()
        // header comment for the rationale; same single bind at
        // scene-build time forwards the engine into every subsequent
        // LayerBuilder.created-via-s.layer(...) call.
        if (ctx.font_engine) s.font_engine(ctx.font_engine);

        // ── Deep magenta+black backdrop with pink vignette accents ──────
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("bg", {
                .size = {1920.0f, 1080.0f},
                .color = {0.04f, 0.02f, 0.06f, 1.0f},
                .fill = FillStyle::linear({0.0f, 0.0f}, {1.0f, 1.0f}, {
                    {0.0f, {0.10f, 0.04f, 0.16f, 1.0f}},
                    {1.0f, {0.02f, 0.02f, 0.04f, 1.0f}},
                }),
            });
            l.vignette(0.40f, 0.65f, 0.55f);
        });

        // ── Streak layers — skinny pink bars that fly past the user ─────
        for (int i = 0; i < 8; ++i) {
            const f32 y = -340.0f + static_cast<f32>(i) * 95.0f;
            s.layer("streak_" + std::to_string(i), [y, i](LayerBuilder& l) {
                const f32 x0 = -2600.0f + 25.0f * static_cast<f32>(i);
                l.position({x0, y, -400.0f});
                l.opacity(0.55f);
                l.rounded_rect("bar", {
                    .size   = {1100.0f, 4.0f},
                    .radius = 2.0f,
                    .color  = {(i % 2 == 0) ? Color{1.0f, 0.35f, 0.55f, 1.0f} : Color{1.0f, 0.85f, 0.45f, 1.0f}},
                });
                // Speed each streak differently so they create the pan blur illusion.
                // Note: l.position_anim() returns AnimatedValue<Vec3>& (not f32),
                // so the timeline must hold Vec3 values throughout.
                const f32 delta_x = 2400.0f + 200.0f * static_cast<f32>(i);
                motion::timeline(Vec3{x0, y, -400.0f})
                    .to(Frame{20}, Vec3{x0 + delta_x, y, -400.0f},
                        EasingCurve{Easing::OutQuad})
                    .hold_until(Frame{90})
                    .apply_to(l.position_anim());
            });
        }

        // ── Camera X position timeline applied directly to AnimatedValue<Vec3>
        // so a single source of truth drives the whip-pan motion.

        // The whip pan punchline — CHRONON3D staggered from frame 22.
        build_text_reveal_line(s, TextRevealDescriptor{
            .text = "CHRONON3D", .font_size = 168.0f, .font_spec = font_bold(),
            .tracking = 6.0f, .base_pos = {0.0f, 0.0f, 0.0f},
            .start_delay = 22.0f, .duration = 8.0f, .stagger = 1.5f,
            .slide_up = true, .slide_up_px = 40.0f,
            .opacity_easing = EasingCurve{Easing::OutCubic},
            .position_easing = EasingCurve{Easing::OutBack},
            .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
            .add_shadow = false, .layer_prefix = "ch"
        });

        // ── Subtitle line snaps in last ────────────────────────────────
        s.layer("subtitle", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.position({0.0f, 200.0f, 0.0f});
            {
                auto& op = l.opacity_anim();
                op.key(Frame{0}, 0.0f, EasingCurve{Easing::Hold});
                op.key(Frame{55}, 0.0f, EasingCurve{Easing::OutCubic});
                op.key(Frame{68}, 1.0f, EasingCurve{Easing::Hold});
            }
            {
                auto& pos = l.position_anim();
                pos.key(Frame{0},  Vec3{ 0.0f, 230.0f, 0.0f}, EasingCurve{Easing::Hold});
                pos.key(Frame{55}, Vec3{ 0.0f, 230.0f, 0.0f}, EasingCurve{Easing::OutCubic});
                pos.key(Frame{68}, Vec3{ 0.0f, 200.0f, 0.0f}, EasingCurve{Easing::Linear});
            }
            auto def = from_text_spec(TextSpec{.content    = {.value = "MOTION BY CAMERA"},.font       = {.font_size = 38.0f},.layout     = {.box = {1100.0f, 80.0f}, .line_height = 1.10f, .tracking = 12.0f},.appearance = {.color = {1.0f, 0.55f, 0.75f, 1.0f}},});
            l.text("subtitle_label", def);
        });

        // ── Apply the whip-pan camera ─────────────────────────────────
        AnimatedValue<Vec3> cam_pos;
        cam_pos.key(Frame{0},    Vec3{-3000.0f, 0.0f, -1000.0f}, EasingCurve{Easing::Linear});
        cam_pos.key(Frame{15},   Vec3{  200.0f, 0.0f, -1000.0f}, EasingCurve{Easing::OutBack});
        cam_pos.key(Frame{22},   Vec3{    0.0f, 0.0f, -1000.0f}, EasingCurve{Easing::OutQuad});
        cam_pos.key(Frame{90},   Vec3{    0.0f, 0.0f, -1000.0f}, EasingCurve{Easing::Linear});
        const Vec3 pos_now = cam_pos.evaluate(ctx.frame);
        Camera2_5D cam;
        cam.enabled = true;
        cam.position = pos_now;
        cam.zoom = 1100.0f;
        cam.fov_deg = 55.0f;
        cam.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
        cam.point_of_interest_enabled = true;
        s.camera().set(cam);

        return s.build();
    });
}

} // namespace chronon3d::content::anims
