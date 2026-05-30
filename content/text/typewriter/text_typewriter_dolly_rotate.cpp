#include "typewriter_common.hpp"

namespace chronon3d::content::text {

Composition text_typewriter_dolly_rotate() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter(
        "TextTypewriterDollyRotate",
        {
            TypewriterLine("DOLLY / 2.5D ROTATE")
                .set_pos({0, -28, 0})
                .set_font(62, 6)
                .set_timing(0, 2.0f)
                .set_color({0.28f, 0.62f, 1.0f, 1.0f}),
            TypewriterLine("the camera moves forward and turns with it.")
                .set_pos({0, 56, 0})
                .set_font(28, 1.0f)
                .set_timing(28, 2.8f)
                .set_color({0.88f, 0.92f, 0.98f, 1.0f})
        },
        presets::motion::MotionPreset::DollyRotate2_5D,
        true,
        {0.010f, 0.012f, 0.020f, 1.0f},
        180,
        1000.0f,
        1280,
        720,
        [](f32 p) {
            return camera_motion::dolly_rotate(p, 1000.0f);
        }
    );
}

} // namespace chronon3d::content::text
