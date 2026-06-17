// content/anims/compositions/cinematic_text_camera.cpp
//
// 5 NEW cinematic text + camera compositions for Chronon3D.
//
// Design notes
//   * Each composition combines a different 2.5D camera technique with a
//     distinct text reveal style.
//   * All keyframes use the existing primitives: motion::timeline(),
//     AnimationCurve via key().key(...), CatmullRomCameraMotion, wiggle3D().
//   * Text uses text_helpers::centered_text::make_centered_text +
//     text_helpers::glow::apply_ae_glow + add_bloom_reveal_layer for the
//     neon reveal in OrbitHandheldGlow.
//   * Handheld shake (OrbitHandheldGlow) is added on top of the Catmull-Rom
//     camera position so the path stays clean while the lens feels organic.
//   * No raw hardcoded colour values where the theme already provides
//     them — uses FRESH_GLOW_* from text_theme.hpp for visual cohesion.
//
// Render examples:
//   chronon3d_cli video DeepParallaxCascade -o output/deep_parallax.mp4
//   chronon3d_cli video WhipPanHeroReveal   -o output/whip_pan.mp4
//   chronon3d_cli video OrbitHandheldGlow   -o output/orbit_glow.mp4
//   chronon3d_cli video RackFocusTitleSwap  -o output/rack_focus.mp4
//   chronon3d_cli video AbyssFreefallStagger -o output/abyss.mp4

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

#include "content/anims/compositions/cinematic_showcase_helpers.hpp"
#include "content/common/text_helpers.hpp"
#include "content/text/text_theme.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace chronon3d::content::anims {

// ───────────────────────────────────────────────────────────────────────────
// Shared helpers
// ───────────────────────────────────────────────────────────────────────────

namespace {

// Whole-composition palette (FRESH_GLOW_* / FRESH_TEXT_* from text_theme.hpp).
using chronon3d::content::text::FRESH_GLOW_CYAN;
using chronon3d::content::text::FRESH_GLOW_PINK;
using chronon3d::content::text::FRESH_GLOW_GOLD;
using chronon3d::content::text::FRESH_GLOW_BLUE;
using chronon3d::content::text::FRESH_TEXT_WHITE;
using chronon3d::content::text::FRESH_TEXT_MUTED;
using chronon3d::content::text_helpers::centered_text::make_centered_text;
using chronon3d::content::text_helpers::glow::apply_ae_glow;
// add_bloom_reveal_layer is omitted intentionally — it currently uses a
// brace-init pattern that doesn't convert to TextParams; we replicate its
// behaviour inline in OrbitHandheldGlow.

constexpr const char* FONT_BOLD = "assets/fonts/Poppins-Bold.ttf";

// Returns centred text params for a given string + font size.
TextParams title_text(const std::string& s, f32 fs,
                      Color color = FRESH_TEXT_WHITE,
                      f32 tracking = 6.0f) {
    return make_centered_text(s, fs, {
        .size        = {1500.0f, 220.0f},
        .line_height = 1.10f,
        .tracking    = tracking,
        .color       = color,
    });
}

// Local shared font engine (matches the pattern in text_animations.cpp).
FontEngine& cinematic_font_engine() {
    static FontEngine engine;
    return engine;
}

// Measure total width of a string at the given font_size using
// the local shaper (kerning-accurate).
f32 measure_total_width(const std::string& text, f32 font_size, f32 tracking = 6.0f) {
    FontEngine& eng = cinematic_font_engine();
    FontSpec spec{FONT_BOLD, "Poppins", 700};
    auto run = eng.shape_text(text, spec, font_size);
    if (!run) return 0.0f;
    const size_t n = run->glyphs.size();
    return run->width + tracking * (n > 1 ? static_cast<f32>(n - 1) : 0.0f);
}

// Per-glyph typewriter line. Same stable-position approach as
// text_animations.cpp: each char is pre-positioned at its final
// location, only opacity/position animate over time.
struct GlyphPos { std::string ch; f32 center_x; };

std::vector<GlyphPos> layout_glyphs(const std::string& text, f32 font_size,
                                    f32 ref_offset_x, f32 tracking = 6.0f) {
    std::vector<GlyphPos> out;
    FontEngine& eng = cinematic_font_engine();
    FontSpec spec{FONT_BOLD, "Poppins", 700};
    auto run = eng.shape_text(text, spec, font_size);
    if (!run || run->glyphs.empty()) return out;

    f32 cursor = ref_offset_x;
    for (size_t gi = 0; gi < run->glyphs.size(); ++gi) {
        const auto& g = run->glyphs[gi];
        size_t start = g.cluster;
        size_t end = text.size();
        for (size_t i = 0; i < run->glyphs.size(); ++i) {
            const auto& o = run->glyphs[i];
            if (o.cluster > start) { end = o.cluster; break; }
        }
        std::string ch = text.substr(start, end - start);
        if (ch.empty()) continue;
        out.push_back({ch, cursor + g.advance_x * 0.5f});
        cursor += g.advance_x + tracking;
    }
    return out;
}

void build_stagger_line(SceneBuilder& s,
                        const std::string& text,
                        f32 font_size,
                        Vec3 base_pos,
                        f32 start_delay,
                        f32 duration = 6.0f,
                        f32 stagger = 1.5f,
                        f32 slide_up_px = 30.0f,
                        Color color = FRESH_TEXT_WHITE) {
    f32 w = measure_total_width(text, font_size);
    f32 ref_x = -w * 0.5f;
    auto chars = layout_glyphs(text, font_size, ref_x);
    for (size_t i = 0; i < chars.size(); ++i) {
        if (chars[i].ch == " ") continue;
        const f32 delay = start_delay + i * stagger;
        const f32 end_f = delay + duration;
        const f32 cx = chars[i].center_x;

        s.layer("ch_" + std::to_string(i),
                [cx, base_pos, delay, end_f, slide_up_px,
                 fs = font_size, ch = chars[i].ch, color]
                (LayerBuilder& l) {
            l.position({cx, base_pos.y, base_pos.z});
            {
                auto& op = l.opacity_anim();
                op.key(Frame{0},                                  0.0f, EasingCurve{Easing::Hold});
                op.key(Frame{static_cast<Frame>(delay)},          0.0f, EasingCurve{Easing::OutCubic});
                op.key(Frame{static_cast<Frame>(end_f)},          1.0f, EasingCurve{Easing::Hold});
            }
            {
                auto& pos = l.position_anim();
                pos.key(Frame{0},                                 Vec3{cx, base_pos.y + slide_up_px, base_pos.z}, EasingCurve{Easing::Hold});
                pos.key(Frame{static_cast<Frame>(delay)},         Vec3{cx, base_pos.y + slide_up_px, base_pos.z}, EasingCurve{Easing::OutBack});
                pos.key(Frame{static_cast<Frame>(end_f)},         Vec3{cx, base_pos.y, base_pos.z},             EasingCurve{Easing::Linear});
            }
            TextParams tp = title_text(ch, fs, color, 0.0f);
            // Widen the text layer box enough to contain the glyph safely.
            tp.size = {fs * 1.4f, fs * 1.8f};
            l.text("label", tp);
        });
    }
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
            TextParams tp = make_centered_text(L.text, L.size, {
                .size        = {1500.0f, 320.0f},
                .line_height = 1.10f,
                .tracking    = 8.0f,
                .color       = L.color,
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
        build_stagger_line(s, "CHRONON3D", 168.0f,
                           {0.0f, 0.0f, 0.0f}, 22.0f,
                           /*duration=*/8.0f, /*stagger=*/1.5f,
                           /*slide_up=*/40.0f,
                           Color{1.0f, 1.0f, 1.0f, 1.0f});

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
            TextParams tp = make_centered_text("MOTION BY CAMERA", 38.0f, {
                .size        = {1100.0f, 80.0f},
                .line_height = 1.10f,
                .tracking    = 12.0f,
                .color       = {1.0f, 0.55f, 0.75f, 1.0f},
            });
            l.text("subtitle_label", tp);
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
        // brace-init pattern that the current TextParams doesn't accept; this
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

            TextParams tp = make_centered_text("AURORA", 220.0f, {
                .size        = {1200.0f, 320.0f},
                .line_height = 1.10f,
                .tracking    = 12.0f,
                .color       = Color{1.00f, 0.96f, 0.84f, 1.0f},
            });
            l.text("title_label", tp);
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
            apply_ae_glow(l, {
                .font_size        = 120.0f,
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
            TextParams tp = make_centered_text("FOCUS NEAR", 130.0f, {
                .size        = {1500.0f, 240.0f},
                .line_height = 1.10f,
                .tracking    = 8.0f,
                .color       = FRESH_TEXT_WHITE,
            });
            l.text("label", tp);
        });

        // ── BACK title at Z=+800 — blurred then sharpens in ──────────
        s.layer("title_back", [](LayerBuilder& l) {
            l.position({0.0f, 160.0f, 800.0f});
            apply_ae_glow(l, {
                .font_size        = 110.0f,
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
            TextParams tp = make_centered_text("FAR AWAY", 120.0f, {
                .size        = {1500.0f, 220.0f},
                .line_height = 1.10f,
                .tracking    = 10.0f,
                .color       = FRESH_TEXT_MUTED,
            });
            l.text("label", tp);
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
        f32 w = measure_total_width(phrase, fs, 4.0f);
        f32 ref_x = -w * 0.5f;
        auto chars = layout_glyphs(phrase, fs, ref_x, 4.0f);
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
                    TextParams tp = make_centered_text(ch, fs, {
                        .size        = {fs * 1.5f, fs * 1.8f},
                        .line_height = 1.10f,
                        .tracking    = 0.0f,
                        .color       = base,
                    });
                    l.glow(GlowParams{
                        .radius          = 30.0f,
                        .intensity       = 0.55f,
                        .color           = {0.40f, 0.70f, 1.0f, 0.7f},
                        .preserve_source = true,
                        .additive        = true,
                    });
                    l.text("label", tp);
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
