#include "../typewriter_common.hpp"

namespace chronon3d::content::text {

Composition tw_glow() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter("TwGlow", {
        TypewriterLine("SOFT GLOW")
            .set_pos({0, 0, 0})
            .set_font(56, 4)
            .set_timing(0, 2.0f)
            .set_color({0.92f, 0.94f, 1.0f, 1})
            .set_align(TextAlign::Center)
            .set_size({1400.0f, 200.0f})
    // glow=false: GlowBloom preset already enables st.effects.glow_enabled
    // via the layer effect system — setting MotionObject.glow would double it.
    }, presets::motion::MotionPreset::GlowBloom, false, {0.01f, 0.012f, 0.022f, 1.0f}, 180, 1100.0f, 1920, 1080);
}

} // namespace chronon3d::content::text
