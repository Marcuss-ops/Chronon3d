// ═══════════════════════════════════════════════════════════════════════════
//  rack_focus_title_swap.cpp — Phase-2.3 split: 1 composition per file.
//
//  Phase-2.3 mechanical extraction (verbatim) of
//  composition rack_focus_title_swap() from the monolithic
//  cinematic_text_camera.cpp (was 667 LOC).  Behaviour preserved
//  bit-identical: bokeh-ish background + FRONT (FOCUS NEAR) + BACK
//  (FAR AWAY) titles with CinematicGlowPreset + 6-keyframe blur
//  timelines + Vertigo dolly-zoom (cam_z closes -1500→-700 while
//  cam_zoom widens 1000→500 in InOutCubic).
//
//  Source-of-truth factory decl: rack_focus_title_swap.hpp
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
// injection; the four names below are referenced unqualified in the
// factory body beyond this point.  Keep scope narrow: only what
// RACK_FOCUS_TITLE_SWAP actually uses (the other 8 declarations from
// the original block live in deep_parallax_cascade / whip_pan_ /
// orbit_ / abyss_ / and are not needed here).
using chronon3d::content::text_reveal::CinematicGlowPreset;
using chronon3d::content::text_reveal::apply_cinematic_glow;
using chronon3d::content::text::FRESH_TEXT_WHITE;
using chronon3d::content::text::FRESH_TEXT_MUTED;

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// 4. RackFocusTitleSwap
//    Vertigo dolly-zoom paired with two opposing blur tracks: a
//    FRONT title snaps sharp while the BACK title blurs out, then the
//    roles swap mid-clip.
// ═══════════════════════════════════════════════════════════════════════════
Composition rack_focus_title_swap() {
    return composition({
        .name     = "RackFocusTitleSwap",
        .width    = 1920,
        .height   = 1080,
        .duration = 180,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // codex/agent2-font-bind-fixes — same single-bind scene-build
        // pattern as deep_parallax_cascade(); see header comment.
        if (ctx.font_engine) s.font_engine(ctx.font_engine);

        // ── Bokeh-ish background: radial purple-to-black ──────────────
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("bg", {
                .size  = {1920.0f, 1080.0f},
                .color = {0.04f, 0.02f, 0.10f, 1.0f},
                .fill  = FillStyle::radial({960.0f, 540.0f}, 900.0f, {
                    {0.0f, {0.20f, 0.10f, 0.30f, 1.0f}},
                    {1.0f, {0.01f, 0.005f, 0.03f, 1.0f}},
                }),
            });
        });

        // ── FRONT title at Z=0 — sharp then blurred out ──────────────
        s.layer("title_front", [](LayerBuilder& l) {
            l.position({0.0f, -180.0f, 0.0f});
            apply_cinematic_glow(l, CinematicGlowPreset{
                .inner_radius     = 5.0f,
                .mid_radius       = 18.0f,
                .bloom_radius     = 36.0f,
                .inner_intensity  = 0.70f,
                .mid_intensity    = 0.30f,
                .bloom_intensity  = 0.12f,
            });
            // Phase 1 (frames 30→60): come into focus from blurred entry.
            // Phase 2 (frames 120→150): rack away out of focus.
            l.blur_anim()
                .key(Frame{0},   15.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{30},  15.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{60},   0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{120},  0.0f, EasingCurve{Easing::Linear})
                .key(Frame{150}, 18.0f, EasingCurve{Easing::InCubic})
                .key(Frame{180}, 18.0f, EasingCurve{Easing::Linear});
            l.opacity_anim()
                .key(Frame{0},  1.0f, EasingCurve{Easing::Hold})
                .key(Frame{30}, 1.0f, EasingCurve{Easing::Linear})
                .key(Frame{150}, 1.0f, EasingCurve{Easing::Linear})
                .key(Frame{180}, 0.7f, EasingCurve{Easing::InCubic});
            auto def = from_text_spec(TextSpec{
                .content    = {.value = "FOCUS NEAR"},
                .font       = {.font_size = 130.0f},
                .layout     = {.box = {1500.0f, 240.0f}, .line_height = 1.10f, .tracking = 8.0f},
                .appearance = {.color = FRESH_TEXT_WHITE},
            });
            l.text("label", def);
        });

        // ── BACK title at Z=+800 — blurred then sharpens in ──────────
        s.layer("title_back", [](LayerBuilder& l) {
            l.position({0.0f, 160.0f, 800.0f});
            apply_cinematic_glow(l, CinematicGlowPreset{
                .inner_radius     = 4.0f,
                .mid_radius       = 16.0f,
                .bloom_radius     = 32.0f,
                .inner_intensity  = 0.60f,
                .mid_intensity    = 0.24f,
                .bloom_intensity  = 0.10f,
            });
            // Phase 1: blurred + low opacity (faded-back).
            // Phase 2 (frames 120→150): rack into focus.
            l.blur_anim()
                .key(Frame{0},   20.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{60},  20.0f, EasingCurve{Easing::Hold})
                .key(Frame{120}, 20.0f, EasingCurve{Easing::Linear})
                .key(Frame{150},  0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{180},  0.0f, EasingCurve{Easing::Linear});
            l.opacity_anim()
                .key(Frame{0},   0.0f, EasingCurve{Easing::Hold})
                .key(Frame{60},  0.55f, EasingCurve{Easing::OutCubic})
                .key(Frame{120}, 0.55f, EasingCurve{Easing::Linear})
                .key(Frame{150}, 1.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{180}, 1.0f, EasingCurve{Easing::Linear});
            auto def = from_text_spec(TextSpec{
                .content    = {.value = "FAR AWAY"},
                .font       = {.font_size = 120.0f},
                .layout     = {.box = {1500.0f, 220.0f}, .line_height = 1.10f, .tracking = 10.0f},
                .appearance = {.color = FRESH_TEXT_MUTED},
            });
            l.text("label", def);
        });

        // ── Camera Vertigo dolly-zoom: close in Z while widening FOV ───
        AnimatedValue<f32> cam_z;
        cam_z.key(Frame{0},  -1500.0f, EasingCurve{Easing::Linear});
        cam_z.key(Frame{30}, -1500.0f, EasingCurve{Easing::Hold});
        cam_z.key(Frame{150}, -700.0f, EasingCurve{Easing::InOutCubic});
        cam_z.key(Frame{180}, -700.0f, EasingCurve{Easing::Linear});

        AnimatedValue<f32> cam_zoom;
        cam_zoom.key(Frame{0},   1000.0f, EasingCurve{Easing::Linear});
        cam_zoom.key(Frame{30},  1000.0f, EasingCurve{Easing::Hold});
        cam_zoom.key(Frame{150},  500.0f, EasingCurve{Easing::InOutCubic});  // widens FOV
        cam_zoom.key(Frame{180},  500.0f, EasingCurve{Easing::Linear});

        Camera2_5D cam;
        cam.enabled = true;
        cam.position = {0.0f, 0.0f, cam_z.evaluate(ctx.frame)};
        cam.zoom = cam_zoom.evaluate(ctx.frame);
        cam.fov_deg = 50.0f;
        cam.point_of_interest = {0.0f, 0.0f, 0.0f};
        cam.point_of_interest_enabled = true;
        s.camera().set(cam);

        return s.build();
    });
}

} // namespace chronon3d::content::anims
