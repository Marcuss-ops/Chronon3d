// ═══════════════════════════════════════════════════════════════════════════
//  product_launch.cpp — End-to-end demo composition for the
//  First-Principles Product Check Test #1 ("demo impossibile da ignorare").
//
//  Combined elements (1 file, 1 Composition, 1 MP4 artifact):
//    1. Animated text   — `l.text(...)` + `l.opacity_anim()` fade-in
//                         (title CHRONON3D, subtitle Launch Day, both
//                         with cinema-side eased keyframes).
//    2. Hero image      — `l.image(...)` with manifest-cleansed
//                         `assets/products/launch_hero.png` asset path
//                         + FitMode::Contain + crossfade-out at midpoint.
//    3. Camera 2.5D     — `s.camera().enable(true).position(...).zoom(...).
//                         point_of_interest(...)` with a parametric
//                         orbit (cos(t)/sin(t)) on a 90-frame loop, ~30
//                         LoC of per-frame math at composition-build time.
//                         (User-spec `scene.camera().orbit(...)` is
//                         Remotion-style; the canonical Chronon3D form
//                         matches dolly_zoom_showcase.cpp's parametric
//                         pattern — see §honesty note in CHANGELOG.)
//    4. Transition      — Cut-crossfade at frame 45 implemented as 2
//                         overlapping layers with mirrored
//                         `l.opacity_anim()` keyframes
//                         (hero 1→0, title 0→1).
//                         (User-spec `scene.transition(...).cut(...)` is
//                         a Remotion-style API; the canonical Chronon3D
//                         crossfade is overlaid opacity-keyframed layers.
//                         §honesty: see CHANGELOG entry.)
//    5. Motion blur     — Wired via the canonical CLI `--motion-blur`
//                         flag (see `apps/chronon3d_cli/commands.hpp`
//                         `RenderQualityArgs::motion_blur`); there is
//                         NO per-layer motion-blur API surface in the
//                         build-time `LayerBuilder` interface, so
//                         motion blur is harness-controlled.
//    6. Audio           — STAGED ASSET ONLY.  No scene.audio() API
//                         exists; the WAV placeholder file
//                         `examples/assets/audio/launch_pad.wav` is
//                         documented in README.md but NOT consumed by
//                         the engine.  §honesty gap: see CHANGELOG entry.
//
//  1920×1080 · 30 fps · 90 frames · canonical CLI:
//    chronon3d_cli video ProductLaunch -o /tmp/product-launch.mp4 \
//                 --start 0 --end 90 --fps 30 --motion-blur
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_definition.hpp>

#include <cmath>
#include <string>

namespace chronon3d::content::launches {

namespace {

// Hero asset path — `assets/products/launch_hero.png` is a user-provided
// image; the preflight gate will surface a MISSING-PATH diagnostic if
// the asset hasn't been provided yet, which is the §honesty behavior
// we want (the gate fails LOUD, not silent).
constexpr const char* kHeroAssetPath = "assets/products/launch_hero.png";

} // anonymous namespace

Composition product_launch() {
    return composition({
        .name     = "ProductLaunch",
        .width    = 1920,
        .height   = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        if (ctx.font_engine) s.font_engine(ctx.font_engine);

        const f32 t   = static_cast<f32>(ctx.frame.integral());
        const f32 fps = static_cast<f32>(ctx.frame_rate.numerator)
                      / static_cast<f32>(ctx.frame_rate.denominator);
        (void)fps;  // fps is exposed via CompositionSpec; kept for debug-prints.

        // ── Background layer — dark cinematic gradient + vignette ─────
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("bg", {
                .size  = {1920.0f, 1080.0f},
                .color = {0.05f, 0.05f, 0.10f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f},
                .fill  = FillStyle::linear({0.0f, 0.0f}, {0.0f, 1.0f}, {
                    {0.0f, {0.06f, 0.06f, 0.10f, 1.0f}},
                    {1.0f, {0.10f, 0.10f, 0.16f, 1.0f}},
                }),
            });
        });

        // ── Element 2: Hero image (with manifest-cleansed asset path) ───
        //   fade-in:  Frame{0..15}  0.0 → 1.0
        //   hold:     Frame{15..45} 1.0
        //   fade-out: Frame{45..60} 1.0 → 0.0  (cut-crossfade partner)
        //   hold:     Frame{60..90} 0.0 (title takes over)
        s.layer("hero", [t](LayerBuilder& l) {
            l.position({0.0f, 60.0f, 0.0f});
            l.opacity_anim()
                .key(Frame{0},   0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{15},  1.0f, EasingCurve{Easing::Hold})
                .key(Frame{45},  1.0f, EasingCurve{Easing::Hold})
                .key(Frame{60},  0.0f, EasingCurve{Easing::InCubic})
                .key(Frame{90},  0.0f, EasingCurve{Easing::Hold});
            l.image("hero_image", ImageParams{
                .asset_path = kHeroAssetPath,  // manifest-cleansed forward (path field is [[deprecated]]; see builder_params.hpp forward-point 0e)
                .size = {1280.0f, 720.0f},
                .fit  = FitMode::Contain,
            });
        });

        // ── Element 1+4: Title text with cut-crossfade at frame 45 ─────
        //   fade-in:  Frame{30..45} 0.0 → 1.0  (overlaps hero fade-out)
        //   hold:     Frame{45..78} 1.0
        //   fade-out: Frame{78..90} 1.0 → 0.0
        s.layer("title", [t](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.position({0.0f, -60.0f, 0.0f});
            l.opacity_anim()
                .key(Frame{0},   0.0f, EasingCurve{Easing::Hold})
                .key(Frame{30},  0.0f, EasingCurve{Easing::Hold})
                .key(Frame{45},  1.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{78},  1.0f, EasingCurve{Easing::Hold})
                .key(Frame{90},  0.0f, EasingCurve{Easing::InSine});
            // Tiny scale-up to give the title reveal weight.
            l.scale_anim()
                .key(Frame{30}, Vec3{0.92f, 0.92f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{45}, Vec3{1.00f, 1.00f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{78}, Vec3{1.00f, 1.00f, 1.0f}, EasingCurve{Easing::Hold})
                .key(Frame{90}, Vec3{0.96f, 0.96f, 1.0f}, EasingCurve{Easing::InSine});
            auto def = from_text_spec(TextSpec{
                .content    = {.value = "CHRONON3D LAUNCH"},
                .font       = {.font_weight = 700,
                                 .font_size = 168.0f},
                .layout     = {.box        = {1600.0f, 280.0f},
                               .line_height = 1.05f,
                               .tracking    = 8.0f},
                .appearance = {.color = Color{1.0f, 1.0f, 1.0f, 1.0f}},
            });
            l.text("title_label", def);
        });

        // ── Subtitle text fade-in late to overlay the title plate ──────
        s.layer("subtitle", [t](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.position({0.0f, 90.0f, 0.0f});
            l.opacity_anim()
                .key(Frame{0},   0.0f, EasingCurve{Easing::Hold})
                .key(Frame{55},  0.0f, EasingCurve{Easing::Hold})
                .key(Frame{70},  1.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{82},  1.0f, EasingCurve{Easing::Hold})
                .key(Frame{90},  0.0f, EasingCurve{Easing::InSine});
            auto def = from_text_spec(TextSpec{
                .content    = {.value = "a deterministic 3D pipeline for media"},
                .font       = {.font_size = 38.0f},
                .layout     = {.box        = {1400.0f, 80.0f},
                               .line_height = 1.10f,
                               .tracking    = 4.0f},
                .appearance = {.color = Color{0.78f, 0.82f, 0.95f, 1.0f}},
            });
            l.text("subtitle_label", def);
        });

        // ── Element 3: camera orbit (parametric 2.5D) ──────────────────
        //   Position traces a 90-frame parametric circle around the hero.
        //   One full revolution per composition.  Vertical offset 60px,
        //   radius 800, push-in feel via the negative-z convention.
        const f32 period_frames = 90.0f;
        const f32 radius        = 800.0f;
        const f32 orbit_phase   =
            2.0f * 3.14159265f * (t / period_frames);
        const f32 cx = radius * std::cos(orbit_phase);
        const f32 cz = -radius * std::sin(orbit_phase);  // push-in axis
        const f32 cy = 60.0f;

        s.camera()
            .enable(true)
            .position({cx, cy, cz})
            .zoom(1400.0f)
            .point_of_interest({0.0f, 0.0f, 0.0f});

        return s.build();
    });
}

} // namespace chronon3d::content::launches
