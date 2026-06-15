#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include "content/common/animation_helpers.hpp"
#include "content/text/text_helpers.hpp"


namespace chronon3d::content::minimalist {

using namespace chronon3d::content::animation_helpers;

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
    return composition({.name = "MinimalistTextTypewriter", .width = 1920, .height = 1080, .duration = 200}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        // 13 chars → chars_per_frame=0.09 → reveal over ~144 frames (72% of duration)
        text::typewriter_build(s, "tw", {
            .text = "SMOOTH REVEAL",
            .box = Vec2{1000.0f, 280.0f},
            .font_size = 100.0f,
            .tracking = 6.0f,
            .line_height = 1.15f,
            .chars_per_frame = 0.09f,
            .start_delay = Frame{20},
            .easing = EasingCurve{Easing::OutCubic},
        }, ctx.frame);
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
        // ~66 chars → chars_per_frame=0.28 → reveal over ~236 frames (79% of duration)
        text::typewriter_build(s, "tw", {
            .text = "Design creates order. Order creates clarity. Clarity creates trust.",
            .box = Vec2{1200.0f, 400.0f},
            .font_size = 64.0f,
            .tracking = 2.0f,
            .line_height = 1.15f,
            .chars_per_frame = 0.28f,
            .start_delay = Frame{20},
            .easing = EasingCurve{Easing::OutSine},
        }, ctx.frame);
        return s.build();
    });
}

Composition minimalist_text_tw_wave() {
    return composition({.name = "MinimalistTextTWWave", .width = 1920, .height = 1080, .duration = 300}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        // ~68 chars → chars_per_frame=0.29 → reveal over ~234 frames (78% of duration)
        text::typewriter_build(s, "tw", {
            .text = "Every great idea moves through rhythm, contrast, space, and balance.",
            .box = Vec2{1100.0f, 350.0f},
            .font_size = 62.0f,
            .tracking = 3.0f,
            .line_height = 1.10f,
            .chars_per_frame = 0.29f,
            .start_delay = Frame{25},
            .easing = EasingCurve{Easing::InOutSine},
        }, ctx.frame);
        return s.build();
    });
}

Composition minimalist_text_tw_snap() {
    return composition({.name = "MinimalistTextTWSnap", .width = 1920, .height = 1080, .duration = 300}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        // ~37 chars, OutExpo snap → chars_per_frame=0.27, snaps fast around frame 60-80
        text::typewriter_build(s, "tw", {
            .text = "Simple. Clear. Immediate. Memorable.",
            .box = Vec2{1200.0f, 350.0f},
            .font_size = 68.0f,
            .tracking = 2.0f,
            .line_height = 1.10f,
            .chars_per_frame = 0.27f,
            .start_delay = Frame{20},
            .easing = EasingCurve{Easing::OutExpo},
        }, ctx.frame);
        return s.build();
    });
}

Composition minimalist_text_tw_drift() {
    return composition({.name = "MinimalistTextTWDrift", .width = 1920, .height = 1080, .duration = 300}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        // ~55 chars → chars_per_frame=0.23 → reveal over ~239 frames (80% of duration)
        text::typewriter_build(s, "tw", {
            .text = "The details are not the details. They make the design.",
            .box = Vec2{1100.0f, 350.0f},
            .font_size = 58.0f,
            .tracking = 4.0f,
            .line_height = 1.18f,
            .chars_per_frame = 0.23f,
            .start_delay = Frame{20},
            .easing = EasingCurve{Easing::InOutCubic},
        }, ctx.frame);
        return s.build();
    });
}

Composition minimalist_text_tw_bold() {
    return composition({.name = "MinimalistTextTWBold", .width = 1920, .height = 1080, .duration = 350}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        // ~81 chars → chars_per_frame=0.30 → reveal over ~270 frames (77% of duration)
        text::typewriter_build(s, "tw", {
            .text = "Good design is as little design as possible. Everything else must earn its place.",
            .box = Vec2{1200.0f, 400.0f},
            .font_size = 54.0f,
            .tracking = 3.0f,
            .line_height = 1.12f,
            .chars_per_frame = 0.30f,
            .start_delay = Frame{25},
            .easing = EasingCurve{Easing::OutCubic},
        }, ctx.frame);
        return s.build();
    });
}

// ── Unicode typewriter — verifies UTF-8 safe reveal ─────────────────────
//
// Uses typewriter_text() (the simple substr-based reveal) with strings
// that contain multi-byte characters: accented letters (è, ï), emoji
// (☕, 🚀, 🎉, ✨), and CJK (世界).  The UTF-8 fix ensures these are
// never split mid-character — each code point is revealed atomically.

Composition minimalist_text_typewriter_unicode() {
    return composition({.name = "MinimalistTextTypewriterUnicode", .width = 1920, .height = 1080, .duration = 280}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);

        // Line 1 — accented + emoji
        s.layer("line1", [&ctx](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("t", text::typewriter_text(
                text::CenterTextOptions{
                    .text = "Caffè ☕ — buongiorno!",
                    .box = Vec2{1400.0f, 160.0f},
                    .pos = Vec3{0.0f, -100.0f, 0.0f},
                    .font_size = 64.0f,
                    .tracking = 3.0f,
                },
                ctx.frame, 1.2f,
                text::TypewriterOptions{
                    .easing = EasingCurve{Easing::OutCubic},
                    .start_delay = Frame{5},
                    .fade_chars = 1.0f,
                }
            ));
        });

        // Line 2 — mixed scripts: CJK + emoji + accented
        s.layer("line2", [&ctx](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("t", text::typewriter_text(
                text::CenterTextOptions{
                    .text = "Hello 世界 🎉 café naïve résumé",
                    .box = Vec2{1400.0f, 160.0f},
                    .pos = Vec3{0.0f, 60.0f, 0.0f},
                    .font_size = 56.0f,
                    .tracking = 2.0f,
                },
                ctx.frame, 1.0f,
                text::TypewriterOptions{
                    .easing = EasingCurve{Easing::InOutCubic},
                    .start_delay = Frame{40},
                    .fade_chars = 1.5f,
                }
            ));
        });

        // Line 3 — emoji-only at larger size
        s.layer("line3", [&ctx](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.text("t", text::typewriter_text(
                text::CenterTextOptions{
                    .text = "🚀✨🌟💫🔥",
                    .box = Vec2{800.0f, 140.0f},
                    .pos = Vec3{0.0f, 200.0f, 0.0f},
                    .font_size = 72.0f,
                    .tracking = 8.0f,
                },
                ctx.frame, 0.6f,
                text::TypewriterOptions{
                    .easing = EasingCurve{Easing::OutExpo},
                    .start_delay = Frame{90},
                    .fade_chars = 2.0f,
                }
            ));
        });

        return s.build();
    });
}

} // namespace chronon3d::content::minimalist
