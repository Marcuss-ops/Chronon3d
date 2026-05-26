#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <cmath>
#include <vector>

#include "text_helpers.hpp"

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
        apply_text(l, name, {
            .text = text,
            .size = size,
            .color = color,
            .align = TextAlign::Center,
            .tracking = tracking,
        }, {W * 0.8f, 160.0f}, pos);
    }
};

using presets::motion::MotionObject;
using presets::motion::TextAlign;

MotionObject make_motion_text(std::string id, std::string text, Vec3 pos, f32 size, f32 tracking, Color color) {
    return MotionObject::text(std::move(id), std::move(text))
        .at(pos)
        .size({1200.0f, 120.0f})
        .font_path("assets/fonts/Georgia_Bold.ttf")
        .font_size(size)
        .tracking(tracking)
        .align(TextAlign::Center)
        .vertical_align(VerticalAlign::Middle)
        .color(color);
}

} // namespace

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

Composition text_premium_motion_pack() {
    return composition({.name = "TextPremiumMotionPack", .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.02f, 0.02f, 0.04f, 1.0f}); });

        std::vector<MotionObject> objs;
        objs.push_back(presets::motion::fade_lift(make_motion_text("f1", "FADE + LIFT", {0, 290, 0}, 84, 10, {0.98f, 0.98f, 0.97f, 1}).time(0, 110)));
        objs.push_back(presets::motion::soft_dolly_reveal(make_motion_text("f2", "SOFT DOLLY REVEAL", {0, 80, 0}, 82, 8, {0.95f, 0.95f, 0.96f, 1}).time(12, 122)));
        objs.push_back(presets::motion::mask_sweep(make_motion_text("f3", "MASK SWEEP", {0, -160, 0}, 88, 12, {0.72f, 0.84f, 1, 1}).time(24, 132)));
        objs.push_back(presets::motion::focus_pull(make_motion_text("f4", "FOCUS PULL", {0, -400, 0}, 86, 10, {0.98f, 0.96f, 0.92f, 1}).time(36, 144)));

        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

} // namespace chronon3d::content::text
