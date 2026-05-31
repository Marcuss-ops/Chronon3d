#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <string>

namespace chronon3d::content::anims {

namespace {

struct MotionElement {
    std::string text;
    f32 font_size{64.0f};

    MotionElement(std::string t) : text(std::move(t)) {}

    void draw(LayerBuilder& l) const {
        l.pin_to(Anchor::Center);
        l.text("label", {
            .text = text, .size = {500, 80},
            .font_size = font_size, .color = {1, 1, 1, 1},
            .align = TextAlign::Center
        });
    }
};

} // namespace

Composition anim_fade_in_text() {
    return composition({.name = "AnimFadeInText", .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text", [&](auto& l) {
            l.opacity(std::min(1.0f, ctx.progress() * 4.0f));
            MotionElement("Fade In").draw(l);
        });
        return s.build();
    });
}

Composition anim_slide_text() {
    return composition({.name = "AnimSlideText", .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.layer("text", [&](auto& l) {
            l.opacity(std::min(1.0f, p * 3.0f)).position({-200.0f + p * 400.0f, 0, 0});
            MotionElement("Slide In").draw(l);
        });
        return s.build();
    });
}

Composition anim_scale_text() {
    return composition({.name = "AnimScaleText", .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.layer("text", [&](auto& l) {
            l.opacity(std::min(1.0f, p * 4.0f)).scale({0.3f + 0.7f * std::min(1.0f, p * 3.0f), 0.3f + 0.7f * std::min(1.0f, p * 3.0f), 1});
            MotionElement("Scale Up").draw(l);
        });
        return s.build();
    });
}

Composition anim_typewriter() {
    return composition({.name = "AnimTypewriter", .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("text", [&](auto& l) {
            MotionElement("Typewriter").draw(l);
        });
        return s.build();
    });
}

} // namespace chronon3d::content::anims

CHRONON_REGISTER_COMPOSITION("AnimFadeInText",  chronon3d::content::anims::anim_fade_in_text)
CHRONON_REGISTER_COMPOSITION("AnimSlideText",   chronon3d::content::anims::anim_slide_text)
CHRONON_REGISTER_COMPOSITION("AnimScaleText",   chronon3d::content::anims::anim_scale_text)
CHRONON_REGISTER_COMPOSITION("AnimTypewriter",  chronon3d::content::anims::anim_typewriter)
