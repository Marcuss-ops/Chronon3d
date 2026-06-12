#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include "content/text/text_helpers.hpp"

namespace chronon3d::content::minimalist {

namespace {

inline void add_black_background(SceneBuilder& s) {
    s.layer("_bg", [](LayerBuilder& l) {
        l.fullscreen_rect("bg", Color{0.0f, 0.0f, 0.0f, 1.0f});
    });
}

} // namespace

Composition minimalist_text_fade() {
    return composition({.name = "MinimalistTextFade", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("phrase", text::centered_text({
                .text = "LESS NOISE",
                .font_size = 100,
                .tracking = 6,
            }));
        });
        return s.build();
    });
}

Composition minimalist_text_rise() {
    return composition({.name = "MinimalistTextRise", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("phrase", text::centered_text({
                .text = "MORE SPACE",
                .font_size = 98,
                .tracking = 7,
            }));
        });
        return s.build();
    });
}

Composition minimalist_text_focus() {
    return composition({.name = "MinimalistTextFocus", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("phrase", text::centered_text({
                .text = "CLEAR FOCUS",
                .font_size = 102,
                .tracking = 5,
            }));
        });
        return s.build();
    });
}

Composition minimalist_text_scale() {
    return composition({.name = "MinimalistTextScale", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("phrase", text::centered_text({
                .text = "STILL FORM",
                .font_size = 102,
                .tracking = 6,
            }));
        });
        return s.build();
    });
}

Composition minimalist_text_drift() {
    return composition({.name = "MinimalistTextDrift", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("phrase", text::centered_text({
                .text = "SLOW BREATH",
                .font_size = 96,
                .tracking = 8,
            }));
        });
        return s.build();
    });
}

Composition minimalist_text_typewriter() {
    return composition({.name = "MinimalistTextTypewriter", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        Frame f = ctx.frame;
        s.layer("phrase", [f](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("phrase", text::typewriter_text({
                .text = "TYPEWRITER TEST",
                .font_size = 110,
                .tracking = 6,
            }, f, 2.0f));
        });
        return s.build();
    });
}

} // namespace chronon3d::content::minimalist
