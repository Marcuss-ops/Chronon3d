#include "typewriter_common.hpp"

namespace chronon3d::content::text {

namespace {

using typewriter::TypewriterLine;
using typewriter::make_typewriter;

Composition make_showcase_segment(std::string name, std::vector<TypewriterLine> lines, Color bg, Frame duration_frames) {
    return make_typewriter(std::move(name), std::move(lines), presets::motion::MotionPreset::PerspectiveSweepTextReveal, true, bg, duration_frames, 1280.0f, 1280, 720);
}

} // namespace

Composition text_typewriter_showcase() {
    return composition({.name = "TextTypewriterShowcase", .width = 1280, .height = 720, .duration = 120}, [](const FrameContext& ctx) {
        const i32 segment = static_cast<i32>(ctx.frame / 30);

        std::vector<TypewriterLine> lines;
        Color bg{0.01f, 0.012f, 0.022f, 1.0f};
        std::string title;

        switch (segment) {
        case 0:
            bg = {0.006f, 0.01f, 0.016f, 1.0f};
            title = "TYPEWRITER / PERSPECTIVE SWEEP";
            lines = {
                TypewriterLine(title).set_pos({0, 0, 0}).set_font(62, 6).set_timing(0, 3.0f).set_color({0.88f, 0.96f, 1, 1}),
            };
            break;
        case 1:
            bg = {0.008f, 0.011f, 0.024f, 1.0f};
            lines = {
                TypewriterLine("WE WRITE THE WORDS").set_pos({0, -56, 0}).set_font(60, 8).set_timing(0, 2.4f).set_color({0.25f, 0.58f, 1, 1}),
                TypewriterLine("THEN THE TEXT WRITES THE SCENE.").set_pos({0, 40, 0}).set_font(42, 3).set_timing(10, 3.4f).set_color({0.92f, 0.94f, 0.98f, 1}),
            };
            break;
        case 2:
            bg = {0.012f, 0.01f, 0.018f, 1.0f};
            lines = {
                TypewriterLine("MANIFEST").set_pos({-W * 0.22f, -98, 0}).set_font(26, 5).set_timing(0, 5.0f).set_color({0.35f, 1, 0.62f, 1}).set_align(TextAlign::Left),
                TypewriterLine("every frame can become a sentence,").set_pos({0, -18, 0}).set_font(40, 1).set_timing(0, 3.6f).set_color({0.95f, 0.96f, 1, 1}),
                TypewriterLine("and every sentence can be typed into motion.").set_pos({0, 46, 0}).set_font(28, 0.6f).set_timing(12, 3.8f).set_color({0.78f, 0.82f, 0.9f, 1}),
            };
            break;
        default:
            bg = {0.009f, 0.012f, 0.02f, 1.0f};
            lines = {
                TypewriterLine("CHAPTER 01").set_pos({0, -72, 0}).set_font(30, 10).set_timing(0, 3.4f).set_color({0.35f, 1, 0.62f, 1}),
                TypewriterLine("THE FIRST WORD ARRIVES").set_pos({0, -4, 0}).set_font(48, 5).set_timing(0, 2.6f).set_color({0.92f, 0.96f, 1, 1}),
                TypewriterLine("typing makes timing visible.").set_pos({0, 62, 0}).set_font(24, 1.2f).set_timing(12, 3.5f).set_color({0.78f, 0.82f, 0.9f, 1}),
            };
            break;
        }

        auto preview = make_showcase_segment("ShowcaseSegment", std::move(lines), bg, 30);
        return preview.evaluate(ctx.frame);
    });
}

} // namespace chronon3d::content::text
