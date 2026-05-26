#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <chronon3d/presets/motion_renderer.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include "text_helpers.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace chronon3d::content::text {

namespace {

using presets::motion::TextAlign;

struct SweepMotion {
    bool enabled{false};
    f32 amp_z{28.0f};
    f32 period{120.0f};

    [[nodiscard]] Vec3 offset(Frame frame, f32 start) const {
        if (!enabled) return {0, 0, 0};
        const f32 t = static_cast<f32>(frame) - start;
        if (t <= 0) return {0, 0, 0};
        return {0, 0, amp_z * std::sin((t / std::max(period, 1.0f)) * 6.283185f)};
    }
};

struct TypewriterLine {
    std::string text;
    Vec3 pos{0, 0, 0};
    f32 font_size{56.0f};
    f32 tracking{0.0f};
    Color color_val{1, 1, 1, 1};
    f32 start_frame{0.0f};
    f32 chars_per_frame{2.0f};
    TextAlign align_val{TextAlign::Center};
    SweepMotion sweep_val{};

    TypewriterLine(std::string t) : text(std::move(t)) {}

    TypewriterLine& set_pos(Vec3 p) { pos = p; return *this; }
    TypewriterLine& set_font(f32 s, f32 t = 0.0f) { font_size = s; tracking = t; return *this; }
    TypewriterLine& set_timing(f32 s, f32 speed = 2.0f) { start_frame = s; chars_per_frame = speed; return *this; }
    TypewriterLine& set_color(Color c) { color_val = c; return *this; }
    TypewriterLine& set_align(TextAlign a) { align_val = a; return *this; }
    TypewriterLine& set_sweep(f32 amp = 28.0f) { sweep_val.enabled = true; sweep_val.amp_z = amp; return *this; }

    [[nodiscard]] std::string reveal(Frame frame) const {
        const f32 t = static_cast<f32>(frame) - start_frame;
        if (t <= 0) return "";
        size_t visible = std::min(text.size(), static_cast<size_t>(t * std::max(chars_per_frame, 0.1f)));
        std::string out = text.substr(0, visible);
        if (visible < text.size() && (frame / 5) % 2 == 0) out += "|";
        return out;
    }
};

Composition make_typewriter(std::string name, std::vector<TypewriterLine> lines, Color bg = {0.01f, 0.012f, 0.022f, 1.0f}) {
    return composition({.name = name, .duration = 180}, [lines = std::move(lines), bg](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = std::clamp(static_cast<f32>(ctx.frame) / 180.0f, 0.0f, 1.0f);
        s.camera().set(camera_motion::dramatic_push(p, 1380.0f));
        
        s.layer("bg", [&](auto& l) { l.fill(bg); });
        s.layer("grid", [&](auto& l) {
            l.opacity(0.12f).grid_background("g", {.size={W,H}, .grid_color={0.18f, 0.5f, 0.96f, 1.0f}, .spacing=96.0f});
        });

        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            std::string revealed = l.reveal(ctx.frame);
            if (revealed.empty()) continue;

            auto obj = presets::motion::MotionObject::text("l" + std::to_string(i), std::move(revealed))
                .at(l.pos + l.sweep_val.offset(ctx.frame, l.start_frame))
                .font_size(l.font_size)
                .color(l.color_val)
                .tracking(l.tracking)
                .align(l.align_val)
                .vertical_align(VerticalAlign::Middle)
                .glow(true)
                .time(0, 180);
            
            presets::motion::perspective_sweep_text_reveal(obj);
            presets::motion::draw_motion_object(s, ctx, obj);
        }
        return s.build();
    });
}

} // namespace

Composition text_typewriter() {
    return make_typewriter("TextTypewriter", {
        TypewriterLine("THE ENGINE LEARNED TO SPEAK.").set_pos({0, -34, 0}).set_font(64, 6).set_timing(0, 1.8f).set_color({0.25f, 0.58f, 1, 1}),
        TypewriterLine("Typed frame by frame, the story becomes motion.").set_pos({0, 54, 0}).set_font(28, 0.5f).set_timing(36, 2.8f).set_color({0.9f, 0.92f, 0.98f, 1})
    });
}

Composition text_typewriter_terminal() {
    return make_typewriter("TextTypewriterTerminal", {
        TypewriterLine("TYPEWRITER / PERSPECTIVE SWEEP").set_pos({0, 0, 0}).set_font(62, 6).set_timing(0, 2.0f).set_color({0.88f, 0.96f, 1, 1})
    }, {0.006f, 0.01f, 0.016f, 1});
}

Composition text_typewriter_quote() {
    return make_typewriter("TextTypewriterQuote", {
        TypewriterLine("WE WRITE THE WORDS").set_pos({0, -56, 0}).set_font(60, 8).set_timing(0, 1.7f).set_color({0.25f, 0.58f, 1, 1}),
        TypewriterLine("THEN THE TEXT WRITES THE SCENE.").set_pos({0, 40, 0}).set_font(42, 3).set_timing(28, 2.4f).set_color({0.92f, 0.94f, 0.98f, 1})
    }, {0.008f, 0.011f, 0.024f, 1});
}

Composition text_typewriter_manifest() {
    return make_typewriter("TextTypewriterManifest", {
        TypewriterLine("MANIFEST").set_pos({-W * 0.22f, -138, 0}).set_font(26, 5).set_timing(0, 4.0f).set_color({0.35f, 1, 0.62f, 1}).set_align(TextAlign::Left),
        TypewriterLine("every frame can become a sentence,").set_pos({0, -38, 0}).set_font(42, 1).set_timing(0, 2.6f).set_color({0.95f, 0.96f, 1, 1}),
        TypewriterLine("and every sentence can be typed into motion.").set_pos({0, 60, 0}).set_font(30, 0.6f).set_timing(26, 2.9f).set_color({0.78f, 0.82f, 0.9f, 1}),
        TypewriterLine("typewriter / scalable / data-driven").set_pos({0, 156, 0}).set_font(24, 4).set_timing(54, 3.5f).set_color({0.35f, 1, 0.62f, 1})
    }, {0.012f, 0.01f, 0.018f, 1});
}

Composition text_typewriter_chapter() {
    return make_typewriter("TextTypewriterChapter", {
        TypewriterLine("CHAPTER 01").set_pos({0, -98, 0}).set_font(34, 10).set_timing(0, 3.0f).set_color({0.35f, 1, 0.62f, 1}),
        TypewriterLine("THE FIRST WORD ARRIVES").set_pos({0, -12, 0}).set_font(54, 5).set_timing(0, 1.9f).set_color({0.92f, 0.96f, 1, 1}),
        TypewriterLine("typing makes timing visible.").set_pos({0, 88, 0}).set_font(26, 1.2f).set_timing(34, 3.0f).set_color({0.78f, 0.82f, 0.9f, 1})
    }, {0.009f, 0.012f, 0.02f, 1});
}

} // namespace chronon3d::content::text
