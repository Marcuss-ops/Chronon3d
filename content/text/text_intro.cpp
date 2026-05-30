#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "text_helpers.hpp"

namespace chronon3d::content::text {

namespace {

using presets::motion::MotionObject;
using presets::motion::MotionPreset;

MotionObject make_title(std::string id, std::string text, Vec3 pos, f32 size, f32 tracking, Color color) {
    return MotionObject::text(std::move(id), std::move(text))
        .at(pos)
        .size({1500.0f, 260.0f})
        .font_path("assets/fonts/Inter-Bold.ttf")
        .font_size(size)
        .tracking(tracking)
        .align(TextAlign::Center)
        .vertical_align(VerticalAlign::Middle)
        .color(color)
        .time(0, 179);
}

MotionObject make_caption(std::string id, std::string text, Vec3 pos, f32 size, f32 tracking, Color color) {
    return MotionObject::text(std::move(id), std::move(text))
        .at(pos)
        .size({1200.0f, 80.0f})
        .font_path("assets/fonts/Inter-Regular.ttf")
        .font_size(size)
        .tracking(tracking)
        .align(TextAlign::Center)
        .vertical_align(VerticalAlign::Middle)
        .color(color)
        .time(0, 179);
}

void add_background(SceneBuilder& s, Color base, Color accent) {
    s.layer("bg", [base](auto& l) { l.fill(base); });
    s.layer("accent_bar", [accent](auto& l) {
        l.opacity(0.8f).pin_to(Anchor::MiddleLeft);
        l.rect("bar", {
            .size = {18.0f, 360.0f},
            .color = accent,
            .pos = {-720.0f, 0.0f, 0.0f},
        });
    });
}

} // namespace

Composition text_intro_clean_reveal() {
    return composition({.name = "TextIntroCleanReveal", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        const f32 pulse = 0.5f + 0.5f * std::sin(p * 6.2831853f * 1.5f);

        add_background(s, {0.017f, 0.019f, 0.030f, 1.0f}, {0.25f, 0.52f, 1.0f, 1.0f});

        s.layer("soft_glow", [&](auto& l) {
            l.opacity(0.18f + 0.08f * pulse).pin_to(Anchor::Center).blur(18.0f);
            l.rounded_rect("blob", {
                .size = {860.0f, 280.0f},
                .radius = 48.0f,
                .color = {0.25f, 0.52f, 1.0f, 0.18f},
                .pos = {0.0f, -18.0f, 0.0f},
            });
        });

        std::vector<MotionObject> objs;
        auto title = make_title("title", "CLEAN TEXT REVEAL", {0.0f, -24.0f, 0.0f}, 98.0f, 8.0f, {0.97f, 0.98f, 1.0f, 1.0f});
        presets::motion::fade_lift(title);
        objs.push_back(std::move(title));
        objs.push_back(MotionObject::text("caption", "Fade in, lift up, and settle.")
            .at({0.0f, 84.0f, 0.0f})
            .size({980.0f, 70.0f})
            .font_path("assets/fonts/Inter-Regular.ttf")
            .font_size(28.0f)
            .tracking(1.5f)
            .align(TextAlign::Center)
            .vertical_align(VerticalAlign::Middle)
            .color({0.82f, 0.86f, 0.94f, 1.0f})
            .preset(MotionPreset::FadeIn)
            .time(0, 179));
        objs.push_back(make_caption("tag", "STARTER PACK", {0.0f, 168.0f, 0.0f}, 18.0f, 6.0f, {0.25f, 0.52f, 1.0f, 0.8f})
            .preset(MotionPreset::SlideUp));

        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

Composition text_intro_sweep_reveal() {
    return composition({.name = "TextIntroSweepReveal", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        add_background(s, {0.008f, 0.010f, 0.018f, 1.0f}, {0.34f, 1.0f, 0.62f, 1.0f});
        s.layer("grid", [](auto& l) {
            l.opacity(0.12f).grid_background("grid", {
                .size = {W, H},
                .grid_color = {0.20f, 0.45f, 0.95f, 1.0f},
                .spacing = 96.0f,
            });
        });

        std::vector<MotionObject> objs;
        auto title = make_title("title", "PERSPECTIVE SWEEP", {0.0f, -30.0f, 0.0f}, 100.0f, 12.0f, {0.93f, 0.97f, 1.0f, 1.0f})
            .glow(true);
        presets::motion::perspective_sweep_text_reveal(title);
        objs.push_back(std::move(title));

        auto subtitle = make_caption("caption", "A cinematic entry with depth, mask and glow.", {0.0f, 84.0f, 0.0f}, 26.0f, 1.5f, {0.74f, 0.83f, 1.0f, 0.9f})
            .preset(MotionPreset::MaskSweep);
        objs.push_back(std::move(subtitle));

        objs.push_back(MotionObject::rect("underline")
            .at({0.0f, 136.0f, 0.0f})
            .size({420.0f, 4.0f})
            .color({0.34f, 1.0f, 0.62f, 0.85f})
            .preset(MotionPreset::FadeIn)
            .time(0, 179));

        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

Composition text_intro_impact_pulse() {
    return composition({.name = "TextIntroImpactPulse", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        const f32 shake = 0.5f + 0.5f * std::sin(p * 6.2831853f * 3.0f);

        add_background(s, {0.010f, 0.010f, 0.016f, 1.0f}, {0.95f, 0.22f, 0.32f, 1.0f});
        s.layer("back_plate", [&](auto& l) {
            l.opacity(0.9f).pin_to(Anchor::Center).blur(12.0f);
            l.rounded_rect("plate", {
                .size = {980.0f, 280.0f},
                .radius = 32.0f,
                .color = {0.95f, 0.22f, 0.32f, 0.12f},
                .pos = {0.0f, 0.0f, 0.0f},
            });
        });

        std::vector<MotionObject> objs;
        auto title = make_title("title", "NEWS IMPACT", {0.0f, -22.0f, 0.0f}, 104.0f, 10.0f, {1.0f, 0.95f, 0.95f, 1.0f})
            .preset(MotionPreset::NewsImpact)
            .glow(true);
        objs.push_back(std::move(title));

        objs.push_back(make_caption("caption", "Punchy headline motion for alerts and promos.", {0.0f, 86.0f, 0.0f}, 26.0f, 1.2f, {0.96f, 0.82f, 0.84f, 0.9f})
            .preset(MotionPreset::FadeIn));

        objs.push_back(MotionObject::rect("pulse_strip")
            .at({-340.0f + 18.0f * shake, 136.0f, 0.0f})
            .size({180.0f, 6.0f})
            .color({0.95f, 0.22f, 0.32f, 0.85f})
            .preset(MotionPreset::SlideLeft)
            .time(0, 179));

        presets::motion::draw_motion_objects(s, ctx, objs);
        return s.build();
    });
}

} // namespace chronon3d::content::text
