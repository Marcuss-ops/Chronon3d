#include "../typewriter_common.hpp"

namespace chronon3d::content::text {

Composition tw_glow_deep() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter("TwGlowDeep", {
        TypewriterLine("DEEP GLOW")
            .set_pos({0, 0, 0})
            .set_font(56, 4)
            .set_timing(0, 2.5f)
            .set_color({0.60f, 0.90f, 1.0f, 1})
            .set_align(TextAlign::Center)
            .set_size({1400.0f, 200.0f})
    // glow=false: GlowBloom preset handles glow via layer effects
    }, presets::motion::MotionPreset::GlowBloom, false, {0.01f, 0.012f, 0.022f, 1.0f}, 200, 1150.0f, 1920, 1080);
}

} // namespace chronon3d::content::text
