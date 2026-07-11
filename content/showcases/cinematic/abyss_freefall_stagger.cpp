// ═══════════════════════════════════════════════════════════════════════════
//  abyss_freefall_stagger.cpp — Phase-2.3 split: 1 composition per file.
//
//  Phase-2.3 mechanical extraction (verbatim) of
//  composition abyss_freefall_stagger() from the monolithic
//  cinematic_text_camera.cpp (was 667 LOC).  Behaviour preserved
//  bit-identical: pure-black bg + far blue torch + per-letter drop
//  stack of the phrase "LET  FALL" using measure_text_width +
//  layout_glyphs to vector-anchor each glyph at z=-150, animated to
//  drop along z to +3500 (Held until per-letter delay = 8 + 6*i,
//  then InCubic drop) with parallel scale compression (1.0 → 0.22)
//  + opacity fade-out.  Camera looks straight down Z axis with a
//  slow roll (pitch -8° → +8°, roll 0° → +45°).
//
//  Source-of-truth factory decl: abyss_freefall_stagger.hpp (1-line
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

namespace {

// Phase-2.3 review-fix — restore anon-namespace `using` declarations
// that the original monolithic cinematic_text_camera.cpp provided via
// implicit using-directive on its enclosing-namespace leakage.  After
// the per-composition .cpp split, this TU no longer inherits that
// injection; the three names below are referenced unqualified in the
// factory body beyond this point.  Keep scope narrow: only what
// ABYSS_FREEFALL_STAGGER actually uses (the other 9 declarations from
// the original block live in deep_parallax_cascade / whip_pan_ /
// orbit_ / rack_focus / and are not needed here).
using chronon3d::content::text_reveal::font_bold;
using chronon3d::content::text_reveal::measure_text_width;
using chronon3d::content::text_reveal::layout_glyphs;

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// 5. AbyssFreefallStagger
//    Camera looks straight down the Z axis. Each letter of the phrase
//    "LET FALL" starts pressed against the lens and drops a long way
//    into the depth while fading out — the camera slowly rolls to
//    amplify the vertigo.
// ═══════════════════════════════════════════════════════════════════════════
Composition abyss_freefall_stagger() {
    return composition({
        .name     = "AbyssFreefallStagger",
        .width    = 1920,
        .height   = 1080,
        .duration = 210,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // codex/agent2-font-bind-fixes — same single-bind scene-build
        // pattern as deep_parallax_cascade(); see header comment.
        if (ctx.font_engine) s.font_engine(ctx.font_engine);

        // Pure black background with a single blue point-light at the
        // far end so the eye has something to fall toward.
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("bg", {
                .size  = {1920.0f, 1080.0f},
                .color = {0.0f, 0.0f, 0.0f, 1.0f},
            });
        });
        s.layer("torch", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 4500.0f});
            l.rounded_rect("glow", {
                .size   = {300.0f, 300.0f},
                .radius = 150.0f,
                .fill   = FillStyle::solid({0.25f, 0.60f, 1.00f, 0.30f}),
            });
        });

        // Map the phrase to glyph positions, but each letter animates
        // Z from -150 (right on the lens) to a deep +3500 with opacity
        // fade-out at the tail.
        const std::string phrase = "LET  FALL";
        const f32 fs = 220.0f;
        auto spec = font_bold();
        f32 w = measure_text_width(phrase, fs, spec, 4.0f, *ctx.font_engine);
        f32 ref_x = -w * 0.5f;
        auto chars = layout_glyphs(phrase, fs, spec, 4.0f, ref_x, *ctx.font_engine);
        for (size_t i = 0; i < chars.size(); ++i) {
            if (chars[i].ch == " ") continue;
            const f32 delay = 8.0f + static_cast<f32>(i) * 6.0f;
            const f32 end_f = delay + 60.0f;
            const f32 cx = chars[i].center_x;
            s.layer("drop_" + std::to_string(i),
                    [cx, delay, end_f, fs, ch = chars[i].ch, i]
                    (LayerBuilder& l) {
                l.position({cx, 0.0f, -150.0f});
                {
                    auto& pos = l.position_anim();
                    // Held at the lens until `delay`, then falls to z=+3500.
                    pos.key(Frame{0},                                 Vec3{cx,     0.0f, -150.0f}, EasingCurve{Easing::Hold});
                    pos.key(Frame{static_cast<Frame>(delay)},         Vec3{cx,     0.0f, -150.0f}, EasingCurve{Easing::Hold});
                    pos.key(Frame{static_cast<Frame>(end_f)},         Vec3{cx,     0.0f,  3500.0f}, EasingCurve{Easing::InCubic});
                }
                {
                    auto& sc = l.scale_anim();
                    // Shrink as it falls (perspective compression).
                    sc.key(Frame{0},                                 Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::Hold});
                    sc.key(Frame{static_cast<Frame>(delay)},         Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::Hold});
                    sc.key(Frame{static_cast<Frame>(end_f)},         Vec3{0.22f, 0.22f, 1.0f}, EasingCurve{Easing::InCubic});
                }
                {
                    auto& op = l.opacity_anim();
                    // Visible until mid-fall, then fade into the abyss.
                    op.key(Frame{0},                                 1.0f, EasingCurve{Easing::Hold});
                    op.key(Frame{static_cast<Frame>(delay)},         1.0f, EasingCurve{Easing::Hold});
                    op.key(Frame{static_cast<Frame>(end_f - 25.0f)}, 0.85f, EasingCurve{Easing::Linear});
                    op.key(Frame{static_cast<Frame>(end_f)},         0.0f, EasingCurve{Easing::InCubic});
                }
                {
                    Color base{0.65f, 0.85f, 1.0f, 1.0f};
                    if (i % 2 == 0) base = Color{0.85f, 0.95f, 1.0f, 1.0f};
                    auto def = from_text_spec(TextSpec{.content = {.value = ch}, .font = {.font_size = fs}, .layout = {.box = {fs * 1.5f, fs * 1.8f}, .line_height = 1.10f, .tracking = 0.0f}, .appearance = {.color = base}});
                    l.glow(GlowParams{
                        .radius          = 30.0f,
                        .intensity       = 0.55f,
                        .color           = {0.40f, 0.70f, 1.0f, 0.7f},
                        .preserve_source = true,
                        .additive        = true,
                    });
                    l.text("label", def);
                }
            });
        }

        // ── Camera: staring straight down Z axis + slow roll + tilt ──────
        // One Vec3 timeline: x = pitch tilt, y = 0, z = roll.
        AnimatedValue<Vec3> cam_rot;
        cam_rot.key(Frame{0},   Vec3{-8.0f, 0.0f,   0.0f}, EasingCurve{Easing::Linear});
        cam_rot.key(Frame{210}, Vec3{ 8.0f, 0.0f,  45.0f}, EasingCurve{Easing::Linear});

        Camera2_5D cam;
        cam.enabled = true;
        cam.position = {0.0f, 0.0f, 0.0f};
        cam.zoom = 1100.0f;
        cam.fov_deg = 55.0f;
        cam.rotation = cam_rot.evaluate(ctx.frame);
        cam.point_of_interest = {0.0f, 0.0f, 4000.0f};
        cam.point_of_interest_enabled = true;
        s.camera().set(cam);

        return s.build();
    });
}

} // namespace chronon3d::content::anims
