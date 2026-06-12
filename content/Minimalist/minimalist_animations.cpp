#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include "content/Minimalist/minimalist_theme.hpp"

namespace chronon3d::content::minimalist {

namespace {

inline void add_black_background(SceneBuilder& s) {
    s.layer("_bg", [](LayerBuilder& l) {
        l.fullscreen_rect("bg", Color{0.0f, 0.0f, 0.0f, 1.0f});
    });
}

inline TextParams make_center_phrase(std::string text, f32 font_size, f32 tracking = 8.0f) {
    return TextParams{
        .text = std::move(text),
        .size = {1200.0f, 240.0f},
        .pos = {0.0f, 0.0f, 0.0f},
        .font_path = FONT_PATH,
        .font_size = font_size,
        .color = TEXT_COLOR_WHITE,
        .align = TextAlign::Center,
        .vertical_align = VerticalAlign::Middle,
        .line_height = 0.95f,
        .tracking = tracking,
        .auto_fit = false,
        .max_lines = 2,
        .min_font_size = MIN_FONT_SIZE,
        .max_font_size = MAX_FONT_SIZE,
        .overflow = TextOverflow::Clip,
        .wrap = TextWrap::Word,
    };
}

} // namespace

Composition minimalist_text_fade() {
    return composition({.name = "MinimalistTextFade", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.opacity(0.0f);
            l.fade_in(Frame{42});
            l.float_idle(4.0f, Frame{150});
            l.text("phrase", make_center_phrase("LESS\nNOISE", 100.0f, 6.0f));
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
            l.fade_shift_vertical(Vec3{0.0f, 96.0f, 0.0f}, Frame{48});
            l.float_idle(6.0f, Frame{150});
            l.text("phrase", make_center_phrase("MORE\nSPACE", 98.0f, 7.0f));
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
            l.focus_in(14.0f, Frame{50});
            l.float_idle(3.0f, Frame{150});
            l.text("phrase", make_center_phrase("CLEAR\nFOCUS", 102.0f, 5.0f));
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
            l.scale_drop(1.08f, Frame{48});
            l.float_idle(5.0f, Frame{150});
            l.text("phrase", make_center_phrase("STILL\nFORM", 102.0f, 6.0f));
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
            l.fade_shift_horizontal(Vec3{-128.0f, 0.0f, 0.0f}, Frame{54}, EasingCurve{Easing::OutCubic});
            l.tracking_breathing(1.02f, Frame{150});
            l.text("phrase", make_center_phrase("SLOW\nBREATH", 96.0f, 8.0f));
        });
        return s.build();
    });
}

} // namespace chronon3d::content::minimalist
