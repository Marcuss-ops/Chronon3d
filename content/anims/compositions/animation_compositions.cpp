#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <string>

namespace chronon3d::content::anims {

namespace {

// ── Common helpers (aligned with Minimalist family conventions) ──────────────

void add_common_background(SceneBuilder& s) {
    s.layer("background", [](auto& l) {
        l.cache_static();
        l.pin_to(Anchor::Center);
        l.grid_background("grid_bg", {
            .size = {1920.0f, 1080.0f},
            .offset = {0.0f, 0.0f},
            .bg_color = {0.025f, 0.027f, 0.031f, 1.0f},
            .grid_color = {0.58f, 0.61f, 0.66f, 0.045f},
            .spacing = 136.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.0f,
            .major_every = 4,
            .centered = true
        });
    });
}

// Common text params matching Minimalist convention — always explicit font_path
// and vertical_align so text renders identically across all composition families.
// Uses a compact box (600x100) appropriate for short single-line labels.
TextParams common_text_params(std::string text, f32 font_size = 64.0f) {
    return TextParams{
        .text = std::move(text),
        .size = {600.0f, 100.0f},
        .pos = {0.0f, 0.0f, 0.0f},
        .font_path = "assets/fonts/Inter-Bold.ttf",
        .font_size = font_size,
        .color = {0.94f, 0.94f, 0.94f, 1.0f},
        .align = TextAlign::Center,
        .vertical_align = VerticalAlign::Middle,
        .line_height = 1.10f,
        .tracking = 0.0f,
        .auto_fit = false,
        .max_lines = 1,
        .min_font_size = 42.0f,
        .max_font_size = 64.0f,
        .overflow = TextOverflow::Clip,
        .wrap = TextWrap::None
    };
}

} // namespace

Composition anim_fade_in_text() {
    return composition({.name = "AnimFadeInText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("text", [&](auto& l) {
            l.pin_to(Anchor::Center);
            l.opacity(std::min(1.0f, ctx.progress() * 4.0f));
            l.text("label", common_text_params("Fade In"));
        });
        return s.build();
    });
}

Composition anim_slide_text() {
    return composition({.name = "AnimSlideText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        const f32 p = ctx.progress();
        s.layer("text", [&](auto& l) {
            l.pin_to(Anchor::Center);
            l.opacity(std::min(1.0f, p * 3.0f)).position({-200.0f + p * 400.0f, 0, 0});
            l.text("label", common_text_params("Slide In"));
        });
        return s.build();
    });
}

Composition anim_scale_text() {
    return composition({.name = "AnimScaleText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        const f32 p = ctx.progress();
        s.layer("text", [&](auto& l) {
            l.pin_to(Anchor::Center);
            l.opacity(std::min(1.0f, p * 4.0f)).scale({0.3f + 0.7f * std::min(1.0f, p * 3.0f), 0.3f + 0.7f * std::min(1.0f, p * 3.0f), 1});
            l.text("label", common_text_params("Scale Up"));
        });
        return s.build();
    });
}

Composition anim_typewriter() {
    return composition({.name = "AnimTypewriter", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_common_background(s);
        s.layer("text", [&](auto& l) {
            l.pin_to(Anchor::Center);
            l.text("label", common_text_params("Typewriter"));
        });
        return s.build();
    });
}

} // namespace chronon3d::content::anims

CHRONON_REGISTER_COMPOSITION("AnimFadeInText",  chronon3d::content::anims::anim_fade_in_text)
CHRONON_REGISTER_COMPOSITION("AnimSlideText",   chronon3d::content::anims::anim_slide_text)
CHRONON_REGISTER_COMPOSITION("AnimScaleText",   chronon3d::content::anims::anim_scale_text)
CHRONON_REGISTER_COMPOSITION("AnimTypewriter",  chronon3d::content::anims::anim_typewriter)
