#include "typewriter_common.hpp"

namespace chronon3d::content::text {

Composition text_typewriter_terminal() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter("TextTypewriterTerminal", {
        TypewriterLine("TYPEWRITER / PERSPECTIVE SWEEP")
            .set_pos({0, 0, 0})
            .set_font(62, 6)
            .set_timing(0, 2.0f)
            .set_color({0.88f, 0.96f, 1, 1})
    }, presets::motion::MotionPreset::PerspectiveSweepTextReveal, true, {0.006f, 0.01f, 0.016f, 1});
}

} // namespace chronon3d::content::text
