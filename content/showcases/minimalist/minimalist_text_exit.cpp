#include "content/showcases/minimalist/minimalist_text_common.hpp"
#include "content/showcases/minimalist/minimalist_text_presets.hpp"

#include <chronon3d/animation/easing/easing.hpp>

namespace chronon3d::content::minimalist {

Composition minimalist_text_fade_away() {
    return make_minimalist_text("MinimalistTextFadeAway", "FADE AWAY", [](LayerBuilder& layer) {
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{115}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 0.0f, EasingCurve{Easing::InCubic});
        layer.scale_anim()
            .key(Frame{0}, Vec3{0.97f, 0.97f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
        layer.position_anim()
            .key(Frame{115}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{0.0f, 12.0f, 0.0f}, EasingCurve{Easing::InCubic});
    });
}

Composition minimalist_text_scale_out() {
    return make_minimalist_text("MinimalistTextScaleOut", "SCALE OUT", [](LayerBuilder& layer) {
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{115}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 0.0f, EasingCurve{Easing::InCubic});
        layer.scale_anim()
            .key(Frame{0}, Vec3{0.95f, 0.95f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{115}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{0.86f, 0.86f, 1.0f}, EasingCurve{Easing::InCubic});
        layer.blur_anim()
            .key(Frame{115}, 0.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 6.0f, EasingCurve{Easing::InCubic});
    });
}

Composition minimalist_text_slide_up_out() {
    return make_minimalist_text("MinimalistTextSlideUpOut", "SLIDE UP OUT", [](LayerBuilder& layer) {
        layer.position_anim()
            .key(Frame{0}, Vec3{0.0f, 20.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{110}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{0.0f, -60.0f, 0.0f}, EasingCurve{Easing::InCubic});
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{110}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 0.0f, EasingCurve{Easing::InCubic});
    });
}

Composition minimalist_text_blur_away() {
    return make_minimalist_text("MinimalistTextBlurAway", "BLUR AWAY", [](LayerBuilder& layer) {
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{115}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 0.0f, EasingCurve{Easing::InCubic});
        layer.scale_anim()
            .key(Frame{0}, Vec3{1.02f, 1.02f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{115}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{1.02f, 1.02f, 1.0f}, EasingCurve{Easing::InCubic});
        layer.blur_anim()
            .key(Frame{115}, 0.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 8.0f, EasingCurve{Easing::InCubic});
    });
}

Composition minimalist_text_tilt_out() {
    return make_minimalist_text("MinimalistTextTiltOut", "TILT OUT", [](LayerBuilder& layer) {
        layer.opacity_anim()
            .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{115}, 1.0f, EasingCurve{Easing::Linear})
            .key(Frame{140}, 0.0f, EasingCurve{Easing::InCubic});
        layer.scale_anim()
            .key(Frame{0}, Vec3{0.97f, 0.97f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{22}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic})
            .key(Frame{115}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{0.94f, 0.94f, 1.0f}, EasingCurve{Easing::InCubic});
        layer.rotate_anim()
            .key(Frame{115}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::Linear})
            .key(Frame{140}, Vec3{0.0f, 0.0f, 3.0f}, EasingCurve{Easing::InCubic});
    });
}

} // namespace chronon3d::content::minimalist
