#include "typewriter_common.hpp"

namespace chronon3d::content::text {

Composition text_typewriter_quote_preview() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter("TextTypewriterQuotePreview", {
        TypewriterLine("WE WRITE THE WORDS")
            .set_pos({0, -56, 0})
            .set_font(60, 8)
            .set_timing(0, 2.0f)
            .set_color({0.25f, 0.58f, 1, 1}),
        TypewriterLine("THEN THE TEXT WRITES THE SCENE.")
            .set_pos({0, 40, 0})
            .set_font(42, 3)
            .set_timing(16, 3.0f)
            .set_color({0.92f, 0.94f, 0.98f, 1})
    }, presets::motion::MotionPreset::PerspectiveSweepTextReveal, true, {0.008f, 0.011f, 0.024f, 1}, 45, 1280.0f, 1280, 720);
}

} // namespace chronon3d::content::text
