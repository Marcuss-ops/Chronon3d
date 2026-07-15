#include "content/showcases/minimalist/minimalist_text_common.hpp"
#include "content/showcases/minimalist/minimalist_text_presets.hpp"

#include <chronon3d/animation/easing/easing.hpp>

namespace chronon3d::content::minimalist {

Composition minimalist_text_fade_up() {
    return make_minimalist_text("MinimalistTextFadeUp", "FADE UP", [](LayerBuilder& layer) {
        layer.position_anim()
            .key(Frame{0}, Vec3{0.0f, 30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_tracking_reveal() {
    return make_minimalist_text(
        "MinimalistTextTrackingReveal", "TRACKING",
        [](LayerBuilder& layer) {
            layer.scale_anim()
                .key(Frame{0}, Vec3{1.18f, 1.0f, 1.0f}, EasingCurve{Easing::OutExpo})
                .key(Frame{24}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutExpo});
            layer.opacity_anim()
                .key(Frame{0}, 0.0f, EasingCurve{Easing::OutExpo})
                .key(Frame{24}, 1.0f, EasingCurve{Easing::Linear});
        },
        MinimalistTextOptions{.font_size = 100.0f, .tracking = 4.0f});
}

Composition minimalist_text_clip_reveal() {
    return make_minimalist_text("MinimalistTextClipReveal", "CLIP REVEAL", [](LayerBuilder& layer) {
        layer.position_anim()
            .key(Frame{0}, Vec3{0.0f, 120.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_fade_down() {
    return make_minimalist_text("MinimalistTextFadeDown", "FADE DOWN", [](LayerBuilder& layer) {
        layer.position_anim()
            .key(Frame{0}, Vec3{0.0f, -30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_soft_scale() {
    return make_minimalist_text(
        "MinimalistTextSoftScale", "SOFT SCALE",
        [](LayerBuilder& layer) {
            layer.scale_anim()
                .key(Frame{0}, Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{16}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
            layer.opacity_anim()
                .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{16}, 1.0f, EasingCurve{Easing::Linear});
        },
        MinimalistTextOptions{.font_size = 100.0f, .tracking = 5.0f});
}

Composition minimalist_text_blur_focus() {
    return make_minimalist_text(
        "MinimalistTextBlurFocus", "BLUR FOCUS",
        [](LayerBuilder& layer) {
            layer.blur_anim()
                .key(Frame{0}, 8.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, 0.0f, EasingCurve{Easing::OutCubic});
            layer.scale_anim()
                .key(Frame{0}, Vec3{1.02f, 1.02f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
            layer.opacity_anim()
                .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
        },
        MinimalistTextOptions{.font_size = 102.0f, .tracking = 5.0f});
}

Composition minimalist_text_slide_left() {
    return make_minimalist_text("MinimalistTextSlideLeft", "SLIDE LEFT", [](LayerBuilder& layer) {
        layer.position_anim()
            .key(Frame{0}, Vec3{-60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_slide_right() {
    return make_minimalist_text("MinimalistTextSlideRight", "SLIDE RIGHT", [](LayerBuilder& layer) {
        layer.position_anim()
            .key(Frame{0}, Vec3{60.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_scale_pop() {
    return make_minimalist_text("MinimalistTextScalePop", "SCALE POP", [](LayerBuilder& layer) {
        layer.scale_anim()
            .key(Frame{0}, Vec3{0.85f, 0.85f, 1.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{16}, Vec3{1.06f, 1.06f, 1.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{20}, Vec3{0.98f, 0.98f, 1.0f}, EasingCurve{Easing::InOutCubic})
            .key(Frame{24}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{16}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_float_in() {
    return make_minimalist_text("MinimalistTextFloatIn", "FLOAT IN", [](LayerBuilder& layer) {
        layer.position_anim()
            .key(Frame{0}, Vec3{0.0f, 80.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        layer.scale_anim()
            .key(Frame{0}, Vec3{0.95f, 0.95f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_letter_rise() {
    return make_minimalist_text("MinimalistTextLetterRise", "LETTER RISE", [](LayerBuilder& layer) {
        layer.scale_anim()
            .key(Frame{0}, Vec3{1.0f, 0.85f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        layer.position_anim()
            .key(Frame{0}, Vec3{0.0f, -10.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_drift_in() {
    return make_minimalist_text("MinimalistTextDriftIn", "DRIFT IN", [](LayerBuilder& layer) {
        layer.position_anim()
            .key(Frame{0}, Vec3{30.0f, 30.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_tilt_in() {
    return make_minimalist_text("MinimalistTextTiltIn", "TILT IN", [](LayerBuilder& layer) {
        layer.rotate_anim()
            .key(Frame{0}, Vec3{0.0f, 0.0f, -3.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
        layer.scale_anim()
            .key(Frame{0}, Vec3{0.97f, 0.97f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{20}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_mask_reveal() {
    return make_minimalist_text("MinimalistTextMaskReveal", "MASK REVEAL", [](LayerBuilder& layer) {
        layer.scale_anim()
            .key(Frame{0}, Vec3{1.0f, 0.6f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{18}, 1.0f, EasingCurve{Easing::Linear});
    });
}

Composition minimalist_text_snap_pop() {
    return make_minimalist_text("MinimalistTextSnapPop", "SNAP POP", [](LayerBuilder& layer) {
        layer.scale_anim()
            .key(Frame{0}, Vec3{0.6f, 0.6f, 1.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{14}, Vec3{1.04f, 1.04f, 1.0f}, EasingCurve{Easing::OutBack})
            .key(Frame{18}, Vec3{0.98f, 0.98f, 1.0f}, EasingCurve{Easing::InOutCubic})
            .key(Frame{22}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{14}, 1.0f, EasingCurve{Easing::Linear});
    });
}

} // namespace chronon3d::content::minimalist
