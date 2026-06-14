#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include "content/text/text_helpers.hpp"

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
        text::typewriter_build(s, "tw", {
            .text = "Fade In",
            .box = {1200.0f, 240.0f},
            .font_size = 64.0f,
            .tracking = 3.0f,
            .chars_per_frame = 999.0f,
            .easing = EasingCurve{Easing::Linear},
        }, ctx.frame);
        return s.build();
    });
}

Composition anim_slide_text() {
    return composition({.name = "AnimSlideText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        text::typewriter_build(s, "tw", {
            .text = "Slide In",
            .box = {1200.0f, 240.0f},
            .font_size = 64.0f,
            .tracking = 3.0f,
            .chars_per_frame = 999.0f,
            .easing = EasingCurve{Easing::Linear},
        }, ctx.frame);
        return s.build();
    });
}

Composition anim_scale_text() {
    return composition({.name = "AnimScaleText", .width = 1920, .height = 1080, .duration = 60}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        text::typewriter_build(s, "tw", {
            .text = "Scale Up",
            .box = {1200.0f, 240.0f},
            .font_size = 64.0f,
            .tracking = 3.0f,
            .chars_per_frame = 999.0f,
            .easing = EasingCurve{Easing::Linear},
        }, ctx.frame);
        return s.build();
    });
}

Composition anim_typewriter() {
    return composition({.name = "AnimTypewriter", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        text::typewriter_build(s, "tw", {
            .text = "Typewriter",
            .box = {1200.0f, 240.0f},
            .font_size = 64.0f,
            .tracking = 3.0f,
            .chars_per_frame = 0.3f,
            .easing = EasingCurve{Easing::OutCubic},
        }, ctx.frame);
        return s.build();
    });
}

} // namespace chronon3d::content::anims
