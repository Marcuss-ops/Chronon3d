// ═══════════════════════════════════════════════════════════════════════════
//  deep_parallax_cascade.cpp — Phase-2.3 split: 1 composition per file.
//
//  Phase-2.3 mechanical extraction (verbatim) of the
//  composition deep_parallax_cascade() from the monolithic
//  cinematic_text_camera.cpp (was 667 LOC).  Behaviour preserved
//  bit-identical: same include set, same `using` directives
//  (FRESH_GLOW_* / FRESH_TEXT_* palette + CinematicGlowPreset
//  chain), same `title_text()` anonymous-namespace helper used
//  only by this composition, same Catmull-Rom Z-push through 4
//  floating text layers + hero brake, same 180-frame duration.
//
//  Source-of-truth factory decl: deep_parallax_cascade.hpp (1-line
//  forward decl) reached either directly or through the
//  cinematic_text_camera.hpp umbrella.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <chronon3d/animation/path/catmull_rom_path.hpp>
#include <chronon3d/animation/effects/wiggle.hpp>
#include <chronon3d/animation/motion/timeline.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/text/text_glow_spec.hpp>
#include <chronon3d/text/font_engine.hpp>

#include "content/showcases/cinematic/cinematic_showcase_helpers.hpp"
#include "content/showcases/cinematic/cinematic_text_camera.hpp"
#include "content/common/text_reveal_helpers.hpp"
#include "content/text/text_helpers.hpp"
#include "content/text/text_theme.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace chronon3d::content::anims {

namespace {

// Whole-composition palette (FRESH_GLOW_* / FRESH_TEXT_* from
// text_theme.hpp).  Per-composition TU-local `using` aliases —
// only deep_parallax_cascade uses FRESH_TEXT_WHITE in this file.
using chronon3d::content::text::FRESH_TEXT_WHITE;

// Returns centred text params for a given string + font size.
// TU-local helper: only this composition uses it; the other 4
// split .cpp files do not share it (kept inline here rather than
// promoted to cinematic_showcase_helpers.hpp because the helper
// is composition-specific in spirit).
auto title_text(const std::string& s, f32 fs,
                      Color color = FRESH_TEXT_WHITE,
                      f32 tracking = 6.0f) {
    return chronon3d::content::text::centered_text({
        .text        = s,
        .box         = {1500.0f, 220.0f},
        .font_size   = fs,
        .tracking    = tracking,
        .color       = color,
        .line_height = 1.10f,
    });
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// 1. DeepParallaxCascade
//    Camera rides a Catmull-Rom path straight along Z, accelerating
//    through four text layers parked at different depths and finally
//    braking onto the hero title.
// ═══════════════════════════════════════════════════════════════════════════
Composition deep_parallax_cascade() {
    return composition({
        .name     = "DeepParallaxCascade",
        .width    = 1920,
        .height   = 1080,
        .duration = 180,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        // codex/agent2-font-bind-fixes — bind the renderer-supplied
        // FontEngine at scene-build time.  Without this explicit call,
        // WP-8 PR 8.0 strict-binding in `materialize_text_run_shape`
        // rejects resolve_engine(engine) lookup (engine = nullptr) and
        // the text layers render blank.  SceneBuilder.cascade-forwards
        // `m_font_engine` onto every LayerBuilder constructed below, so
        // a single bind at scene-build time reaches all text layers in
        // this composition.  Explicit `s.font_engine(ctx.font_engine)`
        // REPLACES any auto-bound pointer (same setter used by
        // SceneBuilder(ctx) ctor auto-forward) — re-assignment is
        // idempotent and safe.
        if (ctx.font_engine) s.font_engine(ctx.font_engine);

        // ── Background: dark with a vertical cyan halo behind the hero ─────
        s.layer("bg_halo", [](LayerBuilder& l) {
            l.rect("halo", {
                .size  = {1920.0f, 1080.0f},
                .color = {0.020f, 0.024f, 0.040f, 1.0f},
                .fill  = FillStyle::radial({960.0f, 540.0f}, 700.0f, {
                    {0.0f, {0.10f, 0.18f, 0.30f, 1.0f}},
                    {1.0f, {0.020f, 0.020f, 0.040f, 1.0f}},
                }),
            });
        });

        // ── Four floating text lines at four Z depths ─────────────────────
        struct FloatingLine {
            const char* text;
            f32 size;
            f32 z;
            Vec3  pos;
            Color color;
        };
        const FloatingLine lines[] = {
            {"FIRST IDEA",      56.0f,  1500.0f, {-200.0f, -260.0f, 1500.0f}, {0.6f, 0.8f, 1.0f, 1.0f}},
            {"BREAKTHROUGH",    72.0f,  1000.0f, { 250.0f,  -80.0f, 1000.0f}, {0.5f, 0.95f, 1.0f, 1.0f}},
            {"PURE MOTION",     64.0f,   500.0f, {-180.0f,  230.0f,  500.0f}, {0.85f, 0.95f, 1.0f, 1.0f}},
            {"CHRONON3D",      180.0f,    0.0f, {   0.0f,    0.0f,    0.0f}, FRESH_TEXT_WHITE},
        };
        for (size_t i = 0; i < 4; ++i) {
            const auto& L = lines[i];
            const f32 blur_peak = (i == 3) ? 0.0f : 14.0f;
            s.layer("line_" + std::to_string(i), [L, blur_peak, i](LayerBuilder& l) {
                l.position(L.pos);
                // Each layer enters sharp at the start and stays sharp
                // (the blur is added only as it FLEES past the camera).
                l.opacity_anim()
                    .key(Frame{0},  0.0f, EasingCurve{Easing::Hold})
                    .key(Frame{20}, 1.0f, EasingCurve{Easing::OutCubic})
                    .key(Frame{static_cast<Frame>(170 - 30.0f * static_cast<f32>(i))}, 1.0f, EasingCurve{Easing::Linear})
                    .key(Frame{180}, 0.0f, EasingCurve{Easing::InCubic});
                l.blur_anim()
                    .key(Frame{0},                                blur_peak, EasingCurve{Easing::Linear})
                    .key(Frame{static_cast<Frame>(130 - 30.0f * i)}, 0.0f,   EasingCurve{Easing::OutCubic})
                    .key(Frame{180},                              0.0f,       EasingCurve{Easing::Linear});
            auto tp = chronon3d::content::text::centered_text({
                .text        = L.text,
                .box         = {1500.0f, 320.0f},
                .font_size   = L.size,
                .tracking    = 8.0f,
                .color       = L.color,
                .line_height = 1.10f,
            });
                l.text("label", tp);
            });
        }

        // ── Camera path: Z axis from +2500 to -400, gentle ease-out ──────
        CatmullRomCameraMotion motion;
        motion.path.set_alpha(CatmullRomAlpha::Centripetal)
                   .set_boundary(CatmullRomBoundary::Clamped)
                   .add_waypoint({   0.0f, 0.0f,  2500.0f})
                   .add_waypoint({   0.0f, 0.0f,  1000.0f})
                   .add_waypoint({   0.0f, 0.0f,  -800.0f})
                   .add_waypoint({   0.0f, 0.0f,  -400.0f});
        motion.auto_orient  = AutoOrientMode::TowardsPOI;
        motion.point_of_interest = {0.0f, 0.0f, 0.0f};
        motion.easing       = EasingCurve{Easing::OutExpo};
        motion.zoom         = 1100.0f;
        motion.fov_deg      = 60.0f;
        s.camera().set(motion.evaluate(ctx.frame, Frame{0}, Frame{179}));

        return s.build();
    });
}

} // namespace chronon3d::content::anims
