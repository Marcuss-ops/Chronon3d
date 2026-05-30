#include "typewriter_common.hpp"

namespace chronon3d::content::text {

Composition text_typewriter_chapter() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter("TextTypewriterChapter", {
        TypewriterLine("CHAPTER 01")
            .set_pos({0, -98, 0})
            .set_font(34, 10)
            .set_timing(0, 3.0f)
            .set_color({0.35f, 1, 0.62f, 1}),
        TypewriterLine("THE FIRST WORD ARRIVES")
            .set_pos({0, -12, 0})
            .set_font(54, 5)
            .set_timing(0, 1.9f)
            .set_color({0.92f, 0.96f, 1, 1}),
        TypewriterLine("typing makes timing visible.")
            .set_pos({0, 88, 0})
            .set_font(26, 1.2f)
            .set_timing(34, 3.0f)
            .set_color({0.78f, 0.82f, 0.9f, 1})
    }, presets::motion::MotionPreset::PerspectiveSweepTextReveal, true, {0.009f, 0.012f, 0.02f, 1});
}

} // namespace chronon3d::content::text
