#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include "content/text/text_helpers.hpp"
#include <chronon3d/text/text_glow_spec.hpp>

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
            auto glow = TextGlowPresets::ae_cinematic_white();
            glow.bloom_radius    = 12.0f;
            glow.mid_radius      = 8.0f;
            glow.bloom_intensity = 0.02f;
            glow.mid_intensity   = 0.06f;
            glow.inner_intensity = 0.18f;
            glow.outer_color     = Color{0.90f, 0.92f, 1.0f, 1.0f};
            l.glow(glow.to_glow_params());
            l.text("phrase", text::typewriter_text({
                .text = "SMOOTH REVEAL",
                .box = Vec2{800.0f, 160.0f},
                .font_size = 110,
                .tracking = 6,
            }, f, 1.5f,
            {
                .easing     = EasingCurve{Easing::OutCubic},
                .start_delay = Frame{15},
                .fade_chars  = 0.8f,
            }));
        });
        return s.build();
    });
}

// ── Typewriter multi-line variations ──────────────────────────────────────
// 5 new typewriter compositions that test word-wrap with longer phrases,
// different easing curves, and slower reveal speeds.

Composition minimalist_text_tw_cascade() {
    return composition({.name = "MinimalistTextTWCascade", .width = 1920, .height = 1080, .duration = 300}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        Frame f = ctx.frame;
        s.layer("phrase", [f](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            auto glow = TextGlowPresets::ae_cinematic_white();
            glow.bloom_radius = 10.0f; glow.mid_radius = 6.0f;
            glow.bloom_intensity = 0.015f; glow.mid_intensity = 0.04f;
            glow.inner_intensity = 0.12f;
            glow.outer_color = Color{0.92f, 0.94f, 1.0f, 1.0f};
            l.glow(glow.to_glow_params());
            l.text("phrase", text::typewriter_text({
                .text = "Design is not just what it looks like. Design is how it works.",
                .box = Vec2{1200.0f, 400.0f},
                .font_size = 64,
                .tracking = 2,
                .max_lines = 0,
                .auto_fit = false,
                .line_height = 1.15f,
            }, f, 0.45f,
            {
                .easing = EasingCurve{Easing::OutSine},
                .start_delay = Frame{20},
                .fade_chars = 0.6f,
            }));
        });
        return s.build();
    });
}

Composition minimalist_text_tw_wave() {
    return composition({.name = "MinimalistTextTWWave", .width = 1920, .height = 1080, .duration = 300}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        Frame f = ctx.frame;
        s.layer("phrase", [f](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            auto glow = TextGlowPresets::ae_cinematic_white();
            glow.bloom_radius = 10.0f; glow.mid_radius = 6.0f;
            glow.bloom_intensity = 0.015f; glow.mid_intensity = 0.04f;
            glow.inner_intensity = 0.12f;
            glow.outer_color = Color{0.92f, 0.94f, 1.0f, 1.0f};
            l.glow(glow.to_glow_params());
            l.text("phrase", text::typewriter_text({
                .text = "Every great design begins with an even better story.",
                .box = Vec2{1100.0f, 350.0f},
                .font_size = 62,
                .tracking = 3,
                .max_lines = 0,
                .auto_fit = false,
                .line_height = 1.10f,
            }, f, 0.40f,
            {
                .easing = EasingCurve{Easing::InOutSine},
                .start_delay = Frame{25},
                .fade_chars = 0.5f,
            }));
        });
        return s.build();
    });
}

Composition minimalist_text_tw_snap() {
    return composition({.name = "MinimalistTextTWSnap", .width = 1920, .height = 1080, .duration = 300}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        Frame f = ctx.frame;
        s.layer("phrase", [f](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            auto glow = TextGlowPresets::ae_cinematic_white();
            glow.bloom_radius = 10.0f; glow.mid_radius = 6.0f;
            glow.bloom_intensity = 0.015f; glow.mid_intensity = 0.04f;
            glow.inner_intensity = 0.12f;
            glow.outer_color = Color{0.92f, 0.94f, 1.0f, 1.0f};
            l.glow(glow.to_glow_params());
            l.text("phrase", text::typewriter_text({
                .text = "Simplicity is the ultimate sophistication. Less is always more.",
                .box = Vec2{1200.0f, 350.0f},
                .font_size = 60,
                .tracking = 2,
                .max_lines = 0,
                .auto_fit = false,
                .line_height = 1.12f,
            }, f, 0.50f,
            {
                .easing = EasingCurve{Easing::OutExpo},
                .start_delay = Frame{30},
                .fade_chars = 0.7f,
            }));
        });
        return s.build();
    });
}

Composition minimalist_text_tw_drift() {
    return composition({.name = "MinimalistTextTWDrift", .width = 1920, .height = 1080, .duration = 300}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        Frame f = ctx.frame;
        s.layer("phrase", [f](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            auto glow = TextGlowPresets::ae_cinematic_white();
            glow.bloom_radius = 10.0f; glow.mid_radius = 6.0f;
            glow.bloom_intensity = 0.015f; glow.mid_intensity = 0.04f;
            glow.inner_intensity = 0.12f;
            glow.outer_color = Color{0.92f, 0.94f, 1.0f, 1.0f};
            l.glow(glow.to_glow_params());
            l.text("phrase", text::typewriter_text({
                .text = "The details are not the details. They make the design.",
                .box = Vec2{1100.0f, 350.0f},
                .font_size = 58,
                .tracking = 4,
                .max_lines = 0,
                .auto_fit = false,
                .line_height = 1.18f,
            }, f, 0.35f,
            {
                .easing = EasingCurve{Easing::InOutCubic},
                .start_delay = Frame{20},
                .fade_chars = 0.5f,
            }));
        });
        return s.build();
    });
}

Composition minimalist_text_tw_bold() {
    return composition({.name = "MinimalistTextTWBold", .width = 1920, .height = 1080, .duration = 300}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        Frame f = ctx.frame;
        s.layer("phrase", [f](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            auto glow = TextGlowPresets::ae_cinematic_white();
            glow.bloom_radius = 10.0f; glow.mid_radius = 6.0f;
            glow.bloom_intensity = 0.015f; glow.mid_intensity = 0.04f;
            glow.inner_intensity = 0.12f;
            glow.outer_color = Color{0.92f, 0.94f, 1.0f, 1.0f};
            l.glow(glow.to_glow_params());
            l.text("phrase", text::typewriter_text({
                .text = "Good design is as little design as possible. Back to simplicity.",
                .box = Vec2{1200.0f, 400.0f},
                .font_size = 66,
                .tracking = 3,
                .max_lines = 0,
                .auto_fit = false,
                .line_height = 1.10f,
            }, f, 0.42f,
            {
                .easing = EasingCurve{Easing::OutCubic},
                .start_delay = Frame{35},
                .fade_chars = 0.6f,
            }));
        });
        return s.build();
    });
}

} // namespace chronon3d::content::minimalist
