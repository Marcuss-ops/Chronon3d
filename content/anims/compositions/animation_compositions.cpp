#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include "content/text/text_helpers.hpp"

#include <string>

namespace chronon3d::content::anims {

namespace {

inline void add_black_background(SceneBuilder& s) {
    s.layer("_bg", [](LayerBuilder& l) {
        l.fullscreen_rect("bg", Color{0.0f, 0.0f, 0.0f, 1.0f});
    });
}

} // namespace

Composition anim_fade_in_text() {
    return composition({.name = "AnimFadeInText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("text", [&](auto& l) {
            l.pin_to(Anchor::Center);
            l.opacity(std::min(1.0f, ctx.progress() * 4.0f));
            l.text("label", text::centered_text({
                .text = "Fade In",
                .font_size = 64.0f,
                .tracking = 3.0f,
            }));
        });
        return s.build();
    });
}

Composition anim_slide_text() {
    return composition({.name = "AnimSlideText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        const f32 p = ctx.progress();
        s.layer("text", [&](auto& l) {
            l.pin_to(Anchor::Center);
            l.opacity(std::min(1.0f, p * 3.0f)).position({-200.0f + p * 400.0f, 0, 0});
            l.text("label", text::centered_text({
                .text = "Slide In",
                .font_size = 64.0f,
                .tracking = 3.0f,
            }));
        });
        return s.build();
    });
}

Composition anim_scale_text() {
    return composition({.name = "AnimScaleText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        const f32 p = ctx.progress();
        s.layer("text", [&](auto& l) {
            l.pin_to(Anchor::Center);
            l.opacity(std::min(1.0f, p * 4.0f)).scale({0.3f + 0.7f * std::min(1.0f, p * 3.0f), 0.3f + 0.7f * std::min(1.0f, p * 3.0f), 1});
            l.text("label", text::centered_text({
                .text = "Scale Up",
                .font_size = 64.0f,
                .tracking = 3.0f,
            }));
        });
        return s.build();
    });
}

Composition anim_typewriter() {
    return composition({.name = "AnimTypewriter", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("text", [&](auto& l) {
            l.pin_to(Anchor::Center);
            l.text("label", text::typewriter_text(
                text::CenterTextOptions{
                    .text = "Typewriter",
                    .font_size = 64.0f,
                    .tracking = 3.0f,
                },
                ctx.frame, 0.3f,
                text::TypewriterOptions{
                    .easing = EasingCurve{Easing::OutCubic},
                    .start_delay = Frame{0},
                    .fade_chars = 1.0f,
                }
            ));
        });
        return s.build();
    });
}

} // namespace chronon3d::content::anims
