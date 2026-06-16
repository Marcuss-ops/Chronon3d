#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/text/text_glow_spec.hpp>
#include "content/common/animation_helpers.hpp"
#include "content/text/text_helpers.hpp"

namespace chronon3d::content::minimalist {

using namespace chronon3d::content::animation_helpers;

// ═════════════════════════════════════════════════════════════════════════════
// 1. FadeUp — il testo sale mentre appare
// ═════════════════════════════════════════════════════════════════════════════
// opacity: 0→1, position_y: +30→0, duration: 18 frame, easing: OutCubic
Composition minimalist_text_fade_up() {
    return composition({.name = "MinimalistTextFadeUp", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.position_anim()
                .key(Frame{0},  Vec3{0.0f, 30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, Vec3{0.0f, 0.0f,  0.0f}, EasingCurve{Easing::OutCubic});
            l.opacity_anim()
                .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
            l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
            l.text("phrase", text::centered_text({
                .text      = "FADE UP",
                .font_size = 100.0f,
                .tracking  = 6.0f,
            }));
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. TrackingReveal — le lettere partono distanziate e si avvicinano
// ═════════════════════════════════════════════════════════════════════════════
// opacity: 0→1, tracking: 24→0, duration: 24 frame, easing: OutExpo
// Simulato con scale_x (1.18→1.0) — l'espansione orizzontale dà lo stesso
// effetto visivo del tracking che si stringe.
Composition minimalist_text_tracking_reveal() {
    return composition({.name = "MinimalistTextTrackingReveal", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.scale_anim()
                .key(Frame{0},  Vec3{1.18f, 1.0f, 1.0f}, EasingCurve{Easing::OutExpo})
                .key(Frame{24}, Vec3{1.0f,  1.0f, 1.0f}, EasingCurve{Easing::OutExpo});
            l.opacity_anim()
                .key(Frame{0},  0.0f, EasingCurve{Easing::OutExpo})
                .key(Frame{24}, 1.0f, EasingCurve{Easing::Linear});
            l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
            l.text("phrase", text::centered_text({
                .text      = "TRACKING",
                .font_size = 100.0f,
                .tracking  = 4.0f,
            }));
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. ClipReveal — il testo sale dal basso con fade-in
// ═════════════════════════════════════════════════════════════════════════════
// position_y: +120→0, opacity: 0→1, duration: 18 frame, easing: OutCubic
Composition minimalist_text_clip_reveal() {
    return composition({.name = "MinimalistTextClipReveal", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.position_anim()
                .key(Frame{0},  Vec3{0.0f, 120.0f, 0.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, Vec3{0.0f, 0.0f,   0.0f}, EasingCurve{Easing::OutCubic});
            l.opacity_anim()
                .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
            l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
            l.text("phrase", text::centered_text({
                .text      = "CLIP REVEAL",
                .font_size = 100.0f,
                .tracking  = 6.0f,
            }));
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. FadeDown — il testo scende dall'alto mentre appare
// ═════════════════════════════════════════════════════════════════════════════
// opacity: 0→1, position_y: -30→0, duration: 18 frame, easing: OutCubic
Composition minimalist_text_fade_down() {
    return composition({.name = "MinimalistTextFadeDown", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.position_anim()
                .key(Frame{0},  Vec3{0.0f, -30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, Vec3{0.0f, 0.0f,   0.0f}, EasingCurve{Easing::OutCubic});
            l.opacity_anim()
                .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
            l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
            l.text("phrase", text::centered_text({
                .text      = "FADE DOWN",
                .font_size = 100.0f,
                .tracking  = 6.0f,
            }));
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. SoftScale — piccolo zoom elegante, senza effetto aggressivo
// ═════════════════════════════════════════════════════════════════════════════
// opacity: 0→1, scale: 0.94→1.0, duration: 16 frame, easing: OutCubic
Composition minimalist_text_soft_scale() {
    return composition({.name = "MinimalistTextSoftScale", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.scale_anim()
                .key(Frame{0},  Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{16}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
            l.opacity_anim()
                .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{16}, 1.0f, EasingCurve{Easing::Linear});
            l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
            l.text("phrase", text::centered_text({
                .text      = "SOFT SCALE",
                .font_size = 100.0f,
                .tracking  = 5.0f,
            }));
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. BlurFocus — il testo parte sfocato e diventa nitido
// ═════════════════════════════════════════════════════════════════════════════
// opacity: 0→1, blur: 8→0, scale: 1.02→1.0, duration: 18 frame, easing: OutCubic
Composition minimalist_text_blur_focus() {
    return composition({.name = "MinimalistTextBlurFocus", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.blur_anim()
                .key(Frame{0},  8.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, 0.0f, EasingCurve{Easing::OutCubic});
            l.scale_anim()
                .key(Frame{0},  Vec3{1.02f, 1.02f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
            l.opacity_anim()
                .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
            l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
            l.text("phrase", text::centered_text({
                .text      = "BLUR FOCUS",
                .font_size = 102.0f,
                .tracking  = 5.0f,
            }));
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. SlideLeft — il testo entra da sinistra
// ═════════════════════════════════════════════════════════════════════════════
// opacity: 0→1, position_x: -60→0, duration: 20 frame, easing: OutCubic
Composition minimalist_text_slide_left() {
    return composition({.name = "MinimalistTextSlideLeft", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.position_anim()
                .key(Frame{0},  Vec3{-60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{20}, Vec3{0.0f,   0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            l.opacity_anim()
                .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{20}, 1.0f, EasingCurve{Easing::Linear});
            l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
            l.text("phrase", text::centered_text({
                .text      = "SLIDE LEFT",
                .font_size = 100.0f,
                .tracking  = 6.0f,
            }));
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. SlideRight — il testo entra da destra
// ═════════════════════════════════════════════════════════════════════════════
// opacity: 0→1, position_x: +60→0, duration: 20 frame, easing: OutCubic
Composition minimalist_text_slide_right() {
    return composition({.name = "MinimalistTextSlideRight", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.position_anim()
                .key(Frame{0},  Vec3{60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{20}, Vec3{0.0f,  0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            l.opacity_anim()
                .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{20}, 1.0f, EasingCurve{Easing::Linear});
            l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
            l.text("phrase", text::centered_text({
                .text      = "SLIDE RIGHT",
                .font_size = 100.0f,
                .tracking  = 6.0f,
            }));
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. ScalePop — piccolo rimbalzo con overshoot morbido
// ═════════════════════════════════════════════════════════════════════════════
// opacity: 0→1, scale: 0.85→1.06→0.98→1.0 (settle), duration: 24 frame
Composition minimalist_text_scale_pop() {
    return composition({.name = "MinimalistTextScalePop", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.scale_anim()
                .key(Frame{0},  Vec3{0.85f, 0.85f, 1.0f}, EasingCurve{Easing::OutBack})
                .key(Frame{16}, Vec3{1.06f, 1.06f, 1.0f}, EasingCurve{Easing::OutBack})
                .key(Frame{20}, Vec3{0.98f, 0.98f, 1.0f}, EasingCurve{Easing::InOutCubic})
                .key(Frame{24}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
            l.opacity_anim()
                .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{16}, 1.0f, EasingCurve{Easing::Linear});
            l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
            l.text("phrase", text::centered_text({
                .text      = "SCALE POP",
                .font_size = 100.0f,
                .tracking  = 6.0f,
            }));
        });
        return s.build();
    });
}

// ═════════════════════════════════════════════════════════════════════════════
// 10. FloatIn — testo leggero che fluttua dentro dal basso
// ═════════════════════════════════════════════════════════════════════════════
// opacity: 0→1, position_y: +80→0, scale: 0.95→1.0, duration: 22 frame
Composition minimalist_text_float_in() {
    return composition({.name = "MinimalistTextFloatIn", .width = 1920, .height = 1080, .duration = 150}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_black_background(s);
        s.layer("phrase", [](LayerBuilder& l) {
            l.pin_to(Anchor::Center);
            l.position_anim()
                .key(Frame{0},  Vec3{0.0f, 80.0f, 0.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{22}, Vec3{0.0f, 0.0f,  0.0f}, EasingCurve{Easing::OutCubic});
            l.scale_anim()
                .key(Frame{0},  Vec3{0.95f, 0.95f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{22}, Vec3{1.0f,  1.0f,  1.0f}, EasingCurve{Easing::OutCubic});
            l.opacity_anim()
                .key(Frame{0},  0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear});
            l.glow(TextGlowPresets::ae_cinematic_white().to_glow_params());
            l.text("phrase", text::centered_text({
                .text      = "FLOAT IN",
                .font_size = 100.0f,
                .tracking  = 6.0f,
            }));
        });
        return s.build();
    });
}

} // namespace chronon3d::content::minimalist
