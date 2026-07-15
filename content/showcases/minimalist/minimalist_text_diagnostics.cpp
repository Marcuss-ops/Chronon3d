#include "content/showcases/minimalist/minimalist_text_common.hpp"
#include "content/showcases/minimalist/minimalist_text_presets.hpp"

#include <chronon3d/animation/easing/easing.hpp>

namespace chronon3d::content::minimalist {

Composition minimalist_text_float_in_no_glow() {
    return make_minimalist_text(
        "MinimalistTextFloatInNoGlow",
        "FLOAT IN",
        [](LayerBuilder& layer) {
            layer.position_anim()
                .key(Frame{0}, Vec3{0.0f, 80.0f, 0.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{22}, Vec3{0.0f, 0.0f, 0.0f}, EasingCurve{Easing::OutCubic});
            layer.scale_anim()
                .key(Frame{0}, Vec3{0.95f, 0.95f, 1.0f}, EasingCurve{Easing::OutCubic})
                .key(Frame{22}, Vec3{1.0f, 1.0f, 1.0f}, EasingCurve{Easing::OutCubic});
            layer.opacity_anim()
                .key(Frame{0}, 0.0f, EasingCurve{Easing::OutCubic})
                .key(Frame{22}, 1.0f, EasingCurve{Easing::Linear});
        },
        MinimalistTextOptions{.glow = false});
}

} // namespace chronon3d::content::minimalist
