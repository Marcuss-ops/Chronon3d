// ═══════════════════════════════════════════════════════════════════════════
//  orbit_handheld_glow.cpp — Phase-2.3 split: 1 composition per file.
//
//  Phase-2.3 mechanical extraction (verbatim) of
//  composition orbit_handheld_glow() from the monolithic
//  cinematic_text_camera.cpp (was 667 LOC).  Behaviour preserved
//  bit-identical: pure-black pitch background + luminous halo
//  behind the title + bloom-settle AURORA title (custom TextGlow
//  preset + scale overshoot) + 8-waypoint closed Catmull-Rom
//  orbit evaluated with `motion.evaluate(ctx.frame, 0, 239)` +
//  wiggle3D shake overlay (seed=42, low-frequency "operator
//  breathing").
//
//  Source-of-truth factory decl: orbit_handheld_glow.hpp (1-line
//  forward decl) reached either directly or through the
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
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/text/text_glow_spec.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_definition.hpp>

#include "content/showcases/cinematic/cinematic_showcase_helpers.hpp"
#include "content/showcases/cinematic/cinematic_text_camera.hpp"
#include "content/common/text_reveal_helpers.hpp"
#include "content/text/text_theme.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace chronon3d::content::anims {

// ═══════════════════════════════════════════════════════════════════════════
// 3. OrbitHandheldGlow
//    Catmull-Rom closed orbit around the title. Wiggle3D shake is added
//    on top of each camera sample so the lens feels like a human
//    operator. The title blooms in from a huge over-exposed halo into
//    a tight neon gold glow.
// ═══════════════════════════════════════════════════════════════════════════
Composition orbit_handheld_glow() {
    return composition({
        .name     = "OrbitHandheldGlow",
        .width    = 1920,
        .height   = 1080,
        .duration = 240,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // codex/agent2-font-bind-fixes — same single-bind scene-build
        // pattern as deep_parallax_cascade(); see header comment.
        if (ctx.font_engine) s.font_engine(ctx.font_engine);

        // Pure black pitch background.
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("bg", {
                .size  = {1920.0f, 1080.0f},
                .color = {0.005f, 0.005f, 0.012f, 1.0f},
            });
        });

        // Floating luminous halo behind the title.
        s.layer("halo", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.rounded_rect("halo", {
                .size   = {900.0f, 320.0f},
                .radius = 160.0f,
                .fill   = FillStyle::radial({0.0f, 0.0f}, 600.0f, {
                    {0.0f, {0.95f, 0.72f, 0.20f, 0.55f}},
                    {1.0f, {0.005f, 0.005f, 0.012f, 0.0f}},
                }),
            });
        });

        // Bloom-settle title (inlined because add_bloom_reveal_layer has a
        // brace-init pattern that the current TextSpec doesn't accept; this
        // gives the same visual effect with explicit field order).
        s.layer("title", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);

            auto glow = TextGlowPresets::ae_cinematic_white();
            glow.inner_radius    = 4.0f  + 90.0f * 0.25f;
            glow.mid_radius      = 26.0f + 90.0f * 0.40f;
            glow.bloom_radius    = 90.0f;
            glow.inner_intensity = 0.85f + 0.95f * 0.50f;
            glow.mid_intensity   = 0.45f + 0.95f * 0.40f;
            glow.bloom_intensity = 0.95f;
            l.glow(glow.to_glow_params());

            // Micro shadow lifted from the cinematic preset.
            l.drop_shadow(Vec2{0.0f, 6.0f},
                          Color{0.10f, 0.04f, 0.0f, 0.40f},
                          /*blur=*/14.0f);

            // Scale overshoot at the start, then settle to 1.0.
            l.scale_anim()
                .key(Frame{0},   Vec3{1.20f, 1.20f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{30},  Vec3{1.05f, 1.05f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{60},  Vec3{1.00f, 1.00f, 1.0f}, EasingCurve{Easing::InOutCubic});

            auto def = from_text_spec(TextSpec{.content = {.value = "AURORA"}, .font = {.font_size = 220.0f}, .layout = {.box = {1200.0f, 320.0f}, .line_height = 1.10f, .tracking = 12.0f}, .appearance = {.color = Color{1.00f, 0.96f, 0.84f, 1.0f}}});
            l.text("title_label", def);
        });
        // ── Camera path: Catmull-Rom closed orbit at radius 1300 ─────
        CatmullRomCameraMotion motion;
        motion.path.set_alpha(CatmullRomAlpha::Centripetal)
                   .set_boundary(CatmullRomBoundary::Closed)
                   .add_waypoints({
                       { 1300.0f,  220.0f,  -650.0f},
                       {  650.0f,  220.0f, -1300.0f},
                       { -650.0f,  220.0f, -1300.0f},
                       {-1300.0f,  220.0f,  -650.0f},
                       {-1300.0f,  220.0f,   650.0f},
                       { -650.0f,  220.0f,  1300.0f},
                       {  650.0f,  220.0f,  1300.0f},
                       { 1300.0f,  220.0f,   650.0f},
                   });
        motion.auto_orient  = AutoOrientMode::TowardsPOI;
        motion.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
        motion.easing       = EasingCurve{Easing::InOutCubic};
        motion.zoom         = 900.0f;
        motion.fov_deg      = 48.0f;
        motion.use_arc_length = true;

        // Handheld shake: low frequency, low amplitude so the user reads
        // it as "operator breathing" not "earthquake".
        const Vec3 shake_offset = wiggle3D(
            2.6f,
            Vec3{36.0f, 22.0f, 28.0f},
            static_cast<f32>(ctx.frame),
            /*seed=*/42
        );

        Camera2_5D cam = motion.evaluate(ctx.frame, Frame{0}, Frame{239});
        cam.position += shake_offset;
        s.camera().set(cam);

        return s.build();
    });
}

} // namespace chronon3d::content::anims
