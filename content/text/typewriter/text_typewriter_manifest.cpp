#include "typewriter_common.hpp"

namespace chronon3d::content::text {

Composition text_typewriter_manifest() {
    using typewriter::TypewriterLine;
    using typewriter::make_typewriter;

    return make_typewriter("TextTypewriterManifest", {
        TypewriterLine("MANIFEST")
            .set_pos({-W * 0.22f, -138, 0})
            .set_font(26, 5)
            .set_timing(0, 4.0f)
            .set_color({0.35f, 1, 0.62f, 1})
            .set_align(TextAlign::Left),
        TypewriterLine("every frame can become a sentence,")
            .set_pos({0, -38, 0})
            .set_font(42, 1)
            .set_timing(0, 2.6f)
            .set_color({0.95f, 0.96f, 1, 1}),
        TypewriterLine("and every sentence can be typed into motion.")
            .set_pos({0, 60, 0})
            .set_font(30, 0.6f)
            .set_timing(26, 2.9f)
            .set_color({0.78f, 0.82f, 0.9f, 1}),
        TypewriterLine("typewriter / scalable / data-driven")
            .set_pos({0, 156, 0})
            .set_font(24, 4)
            .set_timing(54, 3.5f)
            .set_color({0.35f, 1, 0.62f, 1})
    }, presets::motion::MotionPreset::PerspectiveSweepTextReveal, true, {0.012f, 0.01f, 0.018f, 1});
}

} // namespace chronon3d::content::text
