#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <string>
#include <cmath>

#include "text_helpers.hpp"

namespace chronon3d::content::text {

Composition text_credit_roll() {
    return composition({
        .name = "TextCreditRoll",
        .width = 1920, .height = 1080,
        .duration = 300,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 scroll_y = p * 1200.0f + H;

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.005f, 0.008f, 0.020f, 1.0f});
        });

        s.layer("credits_title", [&](LayerBuilder& l) {
            l.position({0, scroll_y - 200, 0});
            apply_text(l, "title", {
                .text = "C R E D I T S",
                .size = 56.0f,
                .color = {0.25f, 0.52f, 1.0f, 1},
                .align = TextAlign::Center,
                .tracking = 14.0f,
            }, {W * 0.6f, 80.0f}, {0, 0, 0});
        });

        struct Credit { const char* role; const char* name; };
        const Credit credits[] = {
            {"Direction", "Pierone"},
            {"Engine", "Chronon3D Team"},
            {"Rendering", "Software Backend"},
            {"Animation", "Motion Presets"},
            {"Typography", "Inter Typeface"},
            {"Pipeline", "Content Generator"},
            {"Testing", "Automated Suite"},
            {"Tools", "CLI Application"},
        };

        for (size_t i = 0; i < 8; ++i) {
            f32 cy = scroll_y - 320.0f - static_cast<f32>(i) * 110.0f;
            s.layer("role_" + std::to_string(i), [&, i, cy](LayerBuilder& l) {
                l.position({0, cy, 0});
                apply_text(l, "role_text", {
                    .text = credits[i].role,
                    .size = 28.0f,
                    .color = {0.6f, 0.6f, 0.7f, 1},
                    .align = TextAlign::Center,
                    .tracking = 4.0f,
                }, {W * 0.4f, 40.0f}, {-200, 0, 0});

                apply_text(l, "name_text", {
                    .text = credits[i].name,
                    .size = 36.0f,
                    .color = {1, 1, 1, 1},
                    .align = TextAlign::Center,
                    .tracking = 1.0f,
                }, {W * 0.4f, 40.0f}, {200, 0, 0});
            });
        }

        return s.build();
    });
}

Composition text_typewriter() {
    return composition({
        .name = "TextTypewriter",
        .width = 1920, .height = 1080,
        .duration = 150,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const std::string full_text =
            "In the beginning, the engine\n"
            "rendered only silence.\n\n"
            "Then came the text pipeline,\n"
            "and with it, the word.";

        f32 p = ctx.progress();
        f32 chars_per_frame = 3.0f;
        size_t visible = std::min(full_text.size(),
            static_cast<size_t>(static_cast<f32>(ctx.frame) * chars_per_frame));
        std::string shown = full_text.substr(0, visible);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.01f, 0.012f, 0.022f, 1.0f});
        });

        f32 cursor_op = 0.5f + 0.5f * std::sin(p * 3.14159f * 8.0f);
        std::string display = shown + (visible < full_text.size()
            ? std::string(1, static_cast<char>(0xE2)) +
              std::string(1, static_cast<char>(0x96)) +
              std::string(1, static_cast<char>(0x88))
            : "");

        s.layer("typewriter", [&](LayerBuilder& l) {
            l.opacity(std::min(1.0f, p * 4.0f))
             .position({-W * 0.35f, 0, 0});
            apply_text(l, "text", {
                .text = display,
                .size = 38.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Left,
                .tracking = 1.0f,
                .line_height = 1.5f,
            }, {W * 0.7f, 300.0f}, {0, 0, 0});
        });

        return s.build();
    });
}

Composition text_animated_sequence() {
    return composition({
        .name = "TextAnimatedSequence",
        .width = 1920, .height = 1080,
        .duration = 180,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 frame = static_cast<f32>(ctx.frame);

        struct SeqItem {
            std::string text;
            f32 start_frame;
            f32 dur;
            f32 size{72.0f};
        };
        const SeqItem items[] = {
            {"PART ONE",          0.0f,  50.0f, 80.0f},
            {"The Journey Begins", 55.0f, 50.0f, 56.0f},
            {"PART TWO",          110.0f, 55.0f, 80.0f},
            {"Resolution",        170.0f, 10.0f, 56.0f},
        };

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.008f, 0.012f, 0.025f, 1.0f});
        });

        for (const auto& item : items) {
            f32 local = frame - item.start_frame;
            f32 fade_in = 10.0f;
            f32 fade_out = 10.0f;
            f32 hold = item.dur - fade_in - fade_out;

            f32 opacity = 0.0f;
            if (local >= 0 && local < fade_in) {
                opacity = local / fade_in;
            } else if (local >= fade_in && local < fade_in + hold) {
                opacity = 1.0f;
            } else if (local >= fade_in + hold && local < item.dur) {
                opacity = 1.0f - (local - fade_in - hold) / fade_out;
            }

            if (opacity > 0.0f) {
                s.layer("seq_" + item.text, [&, opacity, item](LayerBuilder& l) {
                    l.opacity(opacity).pin_to(Anchor::Center);
                    apply_text(l, "text", {
                        .text = item.text,
                        .size = item.size,
                        .color = {1, 1, 1, 1},
                        .align = TextAlign::Center,
                        .tracking = 6.0f,
                    }, {W * 0.8f, 140.0f});
                });
            }
        }

        return s.build();
    });
}

Composition text_countdown() {
    return composition({
        .name = "TextCountdown",
        .width = 1920, .height = 1080,
        .duration = 30,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 frame = static_cast<f32>(ctx.frame);
        f32 count = 3.0f - frame / 10.0f;
        i32 num = std::max(1, static_cast<i32>(std::ceil(count)));
        f32 flicker = 0.9f + 0.1f * std::sin(frame * 0.5f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.02f, 0.02f, 0.04f, 1.0f});
        });

        s.layer("count_num", [&](LayerBuilder& l) {
            l.opacity(flicker).pin_to(Anchor::Center);
            apply_text(l, "num", {
                .text = std::to_string(num),
                .size = 220.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .tracking = 0.0f,
            }, {W * 0.5f, 300.0f});
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text
