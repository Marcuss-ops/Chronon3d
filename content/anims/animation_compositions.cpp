#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d::content::anims {
namespace {

// ── AnimFadeInText ───────────────────────────────────────────────────────
Composition anim_fade_in_text() {
    return composition({
        .name = "AnimFadeInText",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 op = std::min(1.0f, ctx.progress() * 4.0f);

        s.layer("text", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center);
            l.text("label", {
                .text = "Fade In",
                .size = {500, 80}, .pos = {0, 0, 0},
                .font_size = 64, .color = {1,1,1,1},
                .align = TextAlign::Center,
            });
        });
        return s.build();
    });
}

// ── AnimSlideText ────────────────────────────────────────────────────────
Composition anim_slide_text() {
    return composition({
        .name = "AnimSlideText",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 x = -200.0f + p * 400.0f;
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("text", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center).position({x, 0, 0});
            l.text("label", {
                .text = "Slide In",
                .size = {500, 80}, .pos = {0, 0, 0},
                .font_size = 64, .color = {1,1,1,1},
                .align = TextAlign::Center,
            });
        });
        return s.build();
    });
}

// ── AnimScaleText ────────────────────────────────────────────────────────
Composition anim_scale_text() {
    return composition({
        .name = "AnimScaleText",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 sc = 0.3f + 0.7f * std::min(1.0f, p * 3.0f);
        f32 op = std::min(1.0f, p * 4.0f);

        s.layer("text", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center).scale({sc, sc, 1});
            l.text("label", {
                .text = "Scale Up",
                .size = {500, 80}, .pos = {0, 0, 0},
                .font_size = 64, .color = {1,1,1,1},
                .align = TextAlign::Center,
            });
        });
        return s.build();
    });
}

// ── AnimTypewriter ───────────────────────────────────────────────────────
Composition anim_typewriter() {
    return composition({
        .name = "AnimTypewriter",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();

        s.layer("text", [&](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("label", {
                .text = "Typewriter",
                .size = {500, 80}, .pos = {0, 0, 0},
                .font_size = 64, .color = {1,1,1,1},
                .align = TextAlign::Center,
            });
        });
        return s.build();
    });
}

// ── AnimRotateText ───────────────────────────────────────────────────────
Composition anim_rotate_text() {
    return composition({
        .name = "AnimRotateText",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 angle = p * 360.0f;
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("text", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center).rotate({0, 0, angle});
            l.text("label", {
                .text = "Rotate",
                .size = {500, 80}, .pos = {0, 0, 0},
                .font_size = 64, .color = {1,1,1,1},
                .align = TextAlign::Center,
            });
        });
        return s.build();
    });
}

// ── AnimBounceText ───────────────────────────────────────────────────────
Composition anim_bounce_text() {
    return composition({
        .name = "AnimBounceText",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress() * 2.0f;
        f32 t = p - std::floor(p);
        f32 y = -200.0f * t * (1.0f - t) * 4.0f;

        s.layer("text", [&](LayerBuilder& l) {
            l.pin_to(Anchor::Center).position({0, -100 + y, 0});
            l.text("label", {
                .text = "Bounce",
                .size = {500, 80}, .pos = {0, 0, 0},
                .font_size = 64, .color = {1,1,1,1},
                .align = TextAlign::Center,
            });
        });
        return s.build();
    });
}

} // anonymous namespace

void register_all() {}

} // namespace chronon3d::content::anims

CHRONON_REGISTER_COMPOSITION("AnimFadeInText",  chronon3d::content::anims::anim_fade_in_text)
CHRONON_REGISTER_COMPOSITION("AnimSlideText",   chronon3d::content::anims::anim_slide_text)
CHRONON_REGISTER_COMPOSITION("AnimScaleText",   chronon3d::content::anims::anim_scale_text)
CHRONON_REGISTER_COMPOSITION("AnimTypewriter",  chronon3d::content::anims::anim_typewriter)
CHRONON_REGISTER_COMPOSITION("AnimRotateText",  chronon3d::content::anims::anim_rotate_text)
CHRONON_REGISTER_COMPOSITION("AnimBounceText",  chronon3d::content::anims::anim_bounce_text)
