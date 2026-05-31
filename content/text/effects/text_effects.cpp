#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <cmath>
#include <vector>

#include "../helpers/text_helpers.hpp"

namespace chronon3d::content::text {

namespace {

struct EffectLine {
    std::string text;
    f32 size{100.0f};
    Color color{1, 1, 1, 1};
    f32 tracking{8.0f};
    Vec3 pos{0, 0, 0};

    EffectLine(std::string t) : text(std::move(t)) {}

    EffectLine& set_size(f32 s) { size = s; return *this; }
    EffectLine& set_color(Color c) { color = c; return *this; }
    EffectLine& set_tracking(f32 t) { tracking = t; return *this; }
    EffectLine& set_pos(Vec3 p) { pos = p; return *this; }

    void draw(LayerBuilder& l, const std::string& name = "text") const {
        apply_text(l, name, TextDef{
            .text = text,
            .size = size,
            .color = color,
            .align = TextAlign::Center,
            .tracking = tracking,
        }, {W * 0.8f, 160.0f}, pos);
    }
};

using presets::motion::MotionObject;

MotionObject make_motion_text(std::string id, std::string text, Vec3 pos, f32 size, f32 tracking, Color color) {
    return MotionObject::text(std::move(id), std::move(text))
        .at(pos)
        // Give the glyphs enough vertical room so large titles and glow don't clip.
        .size({1500.0f, 260.0f})
        .font_path("assets/fonts/Georgia_Bold.ttf")
        .font_size(size)
        .tracking(tracking)
        .align(TextAlign::Center)
        .vertical_align(VerticalAlign::Middle)
        .color(color);
}

} // namespace

using presets::motion::MotionObject;
using presets::motion::MotionPreset;

Composition text_glow() {
    return composition({.name = "TextGlow", .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 pulse = 0.6f + 0.4f * std::sin(ctx.progress() * 3.14159f * 4.0f);
        
        s.layer("bg", [](auto& l) { l.fill({0.005f, 0.005f, 0.02f, 1.0f}); });
        s.layer("glow_text", [&](auto& l) {
            l.opacity(std::min(1.0f, ctx.progress() * 3.0f)).pin_to(Anchor::Center);
            l.glow(40.0f * pulse, pulse, {0.25f, 0.52f, 1.0f});
            EffectLine("GLOW EFFECT").set_size(110).set_tracking(8).draw(l);
        });
        return s.build();
    });
}

Composition text_shadow() {
    return composition({.name = "TextShadow", .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.05f, 0.05f, 0.06f, 1.0f}); });
        s.layer("shadow_text", [&](auto& l) {
            l.opacity(std::min(1.0f, ctx.progress() * 3.0f)).pin_to(Anchor::Center);
            l.drop_shadow({6, 10}, {0, 0, 0, 0.55f}, 16);
            EffectLine("DROP SHADOW").set_size(100).set_tracking(5).draw(l);
        });
        return s.build();
    });
}

Composition text_pulse() {
    return composition({.name = "TextPulse", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 pulse = 0.5f + 0.5f * std::sin(p * 6.2832f * 2.0f);
        f32 scale = 1.0f + 0.05f * std::sin(p * 6.2832f * 2.0f);
        f32 op = std::min(1.0f, p * 4.0f);

        s.layer("bg", [](auto& l) { l.fill({0.008f, 0.010f, 0.025f, 1.0f}); });
        s.layer("pulse_text", [&](auto& l) {
            l.opacity(op * pulse).pin_to(Anchor::Center).scale({scale, scale, 1});
            EffectLine("PULSING").set_size(100).set_color({0.25f, 0.52f, 1.0f, 1}).set_tracking(10).draw(l);
        });
        s.layer("sub_pulse", [&](auto& l) {
            l.opacity(op * (1.0f - pulse) * 0.6f).pin_to(Anchor::Center);
            EffectLine("Pulse animation via opacity & scale").set_size(28).set_color({0.8f, 0.8f, 0.85f, 1}).set_tracking(2).set_pos({0, 100, 0}).draw(l, "sub");
        });
        return s.build();
    });
}

Composition text_fade_lift_demo() {
    return composition({.name = "TextFadeLiftDemo", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.02f, 0.02f, 0.04f, 1.0f}); });

        std::vector<MotionObject> objs;
        objs.push_back(presets::motion::fade_lift(
            make_motion_text("fade_lift", "FADE + LIFT", {0, 0, 0}, 96, 10, {0.98f, 0.98f, 0.97f, 1}).time(0, 179)
        ));
        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

Composition text_soft_dolly_reveal_demo() {
    return composition({.name = "TextSoftDollyRevealDemo", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.02f, 0.02f, 0.04f, 1.0f}); });

        std::vector<MotionObject> objs;
        objs.push_back(presets::motion::soft_dolly_reveal(
            make_motion_text("soft_dolly", "SOFT DOLLY REVEAL", {0, 0, 0}, 92, 8, {0.95f, 0.95f, 0.96f, 1}).time(0, 179)
        ));
        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

Composition text_mask_sweep_demo() {
    return composition({.name = "TextMaskSweepDemo", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.02f, 0.02f, 0.04f, 1.0f}); });

        std::vector<MotionObject> objs;
        objs.push_back(presets::motion::mask_sweep(
            make_motion_text("mask_sweep", "MASK SWEEP", {0, 0, 0}, 98, 12, {0.72f, 0.84f, 1.0f, 1}).time(0, 179)
        ));
        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

Composition text_focus_pull_demo() {
    return composition({.name = "TextFocusPullDemo", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.02f, 0.02f, 0.04f, 1.0f}); });

        std::vector<MotionObject> objs;
        objs.push_back(presets::motion::focus_pull(
            make_motion_text("focus_pull", "FOCUS PULL", {0, 0, 0}, 96, 10, {0.98f, 0.96f, 0.92f, 1}).time(0, 179)
        ));
        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

Composition text_glow_bloom_demo() {
    return composition({.name = "TextGlowBloomDemo", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.015f, 0.015f, 0.03f, 1.0f}); });

        std::vector<MotionObject> objs;
        objs.push_back(presets::motion::glow_bloom(
            make_motion_text("glow_bloom", "GLOW BLOOM", {0, 0, 0}, 100, 12, {0.96f, 0.97f, 1.0f, 1}).time(0, 179)
        ));
        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

Composition text_stagger_reveal_demo() {
    return composition({.name = "TextStaggerRevealDemo", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.02f, 0.02f, 0.04f, 1.0f}); });

        std::vector<MotionObject> objs;
        objs.push_back(presets::motion::stagger_reveal(
            make_motion_text("stagger_reveal", "STAGGERED REVEAL", {0, 0, 0}, 90, 10, {0.96f, 0.96f, 0.98f, 1}).time(0, 179)
        ));
        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

Composition text_orbit_2_5d_demo() {
    return composition({.name = "TextOrbit2_5DDemo", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable()
                  .position({0.0f, 0.0f, -1000.0f})
                  .zoom(1000.0f)
                  .point_of_interest({0.0f, 0.0f, 0.0f});
        s.layer("bg", [](auto& l) { l.fill({0.01f, 0.01f, 0.025f, 1.0f}); });

        std::vector<MotionObject> objs;
        auto obj = make_motion_text("orbit_text", "ORBIT", {0, 0, 0}, 108, 18, {0.50f, 0.78f, 1.0f, 1.0f})
            .time(0, 179)
            .preset(MotionPreset::Orbit2_5D)
            .enable_3d()
            .glow(true);
        objs.push_back(std::move(obj));

        auto sub = make_motion_text("orbit_sub", "2.5D ORBIT MOTION", {0, 130, 0}, 34, 8, {0.65f, 0.82f, 1.0f, 0.7f})
            .time(0, 179)
            .preset(MotionPreset::FadeLift)
            .size({1500.0f, 80.0f});
        objs.push_back(std::move(sub));

        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

Composition text_tilt_sweep_2_5d_demo() {
    return composition({.name = "TextTiltSweep2_5DDemo", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable()
                  .position({0.0f, 0.0f, -1000.0f})
                  .zoom(1000.0f)
                  .point_of_interest({0.0f, 0.0f, 0.0f});
        s.layer("bg", [](auto& l) { l.fill({0.01f, 0.015f, 0.01f, 1.0f}); });

        std::vector<MotionObject> objs;
        auto obj = make_motion_text("tilt_text", "TILT SWEEP", {0, 0, 0}, 100, 16, {0.72f, 1.0f, 0.75f, 1.0f})
            .time(0, 179)
            .glow(true);
        presets::motion::tilt_sweep_2_5d(
            obj,
            {0.0f, 0.0f, 240.0f},   // position amplitude (z push)
            {10.0f, 18.0f, 5.0f},    // rotation amplitude (x, y, z tilt)
            150.0f,                   // duration frames
            0.0f,                     // start delay
            true                      // one-shot
        );
        objs.push_back(std::move(obj));

        auto sub = make_motion_text("tilt_sub", "2.5D TILT SWEEP", {0, 130, 0}, 34, 6, {0.72f, 1.0f, 0.75f, 0.65f})
            .time(0, 179)
            .preset(MotionPreset::FadeLift)
            .size({1500.0f, 80.0f});
        objs.push_back(std::move(sub));

        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

Composition text_motion_trio_demo() {
    return composition({.name = "TextMotionTrioDemo", .duration = 480}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable()
                  .position({0.0f, 0.0f, -1000.0f})
                  .zoom(1000.0f)
                  .point_of_interest({0.0f, 0.0f, 0.0f});

        // Gradient background that shifts across the three segments
        const f32 p = ctx.progress();
        const Color bg_a{0.01f, 0.01f, 0.025f, 1.0f};
        const Color bg_b{0.01f, 0.015f, 0.01f, 1.0f};
        const Color bg_c{0.018f, 0.008f, 0.025f, 1.0f};

        auto lerp_c = [](Color a, Color b, f32 t) -> Color {
            return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0f};
        };

        Color bg = p < 0.33f ? lerp_c(bg_a, bg_b, p / 0.33f)
                 : p < 0.66f ? lerp_c(bg_b, bg_c, (p - 0.33f) / 0.33f)
                              : lerp_c(bg_c, bg_a, (p - 0.66f) / 0.34f);

        s.layer("bg", [bg](auto& l) { l.fill(bg); });

        std::vector<MotionObject> objs;

        // ── Segment 1: Orbit2_5D (frames 0–159) ──────────────────────────────
        if (ctx.frame < 160) {
            auto title = make_motion_text("orbit_title", "ORBIT", {0, -40, 0}, 108, 18, {0.50f, 0.78f, 1.0f, 1.0f})
                .time(0, 159)
                .preset(MotionPreset::Orbit2_5D)
                .enable_3d()
                .glow(true);
            objs.push_back(std::move(title));

            auto label = make_motion_text("orbit_label", "Orbit2_5D", {0, 80, 0}, 32, 8, {0.50f, 0.78f, 1.0f, 0.6f})
                .time(0, 159)
                .preset(MotionPreset::FadeIn)
                .size({1500.0f, 70.0f});
            objs.push_back(std::move(label));
        }

        // ── Segment 2: TiltSweep2_5D (frames 160–319) ────────────────────────
        if (ctx.frame >= 160 && ctx.frame < 320) {
            auto title = make_motion_text("tilt_title", "TILT SWEEP", {0, -40, 0}, 100, 16, {0.72f, 1.0f, 0.75f, 1.0f})
                .time(160, 319)
                .glow(true);
            presets::motion::tilt_sweep_2_5d(title, {0.0f, 0.0f, 240.0f}, {10.0f, 18.0f, 5.0f}, 140.0f, 0.0f, true);
            objs.push_back(std::move(title));

            auto label = make_motion_text("tilt_label", "TiltSweep2_5D", {0, 80, 0}, 32, 8, {0.72f, 1.0f, 0.75f, 0.6f})
                .time(160, 319)
                .preset(MotionPreset::FadeIn)
                .size({1500.0f, 70.0f});
            objs.push_back(std::move(label));
        }

        // ── Segment 3: PerspectiveSweepTextReveal (frames 320–479) ───────────
        if (ctx.frame >= 320) {
            auto title = make_motion_text("persp_title", "PERSPECTIVE", {0, -40, 0}, 104, 14, {0.96f, 0.80f, 1.0f, 1.0f})
                .time(320, 479)
                .glow(true);
            presets::motion::perspective_sweep_text_reveal(title);
            objs.push_back(std::move(title));

            auto label = make_motion_text("persp_label", "PerspectiveSweepTextReveal", {0, 80, 0}, 28, 6, {0.96f, 0.80f, 1.0f, 0.6f})
                .time(320, 479)
                .preset(MotionPreset::FadeIn)
                .size({1500.0f, 70.0f});
            objs.push_back(std::move(label));
        }

        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

} // namespace chronon3d::content::text
