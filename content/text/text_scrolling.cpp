#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <string>
#include <cmath>
#include <vector>

#include "text_helpers.hpp"

namespace chronon3d::content::text {

namespace {

struct CreditItem {
    std::string role;
    std::string name;

    void draw(LayerBuilder& l, f32 y) const {
        l.position({0, y, 0});
        apply_text(l, "role", TextDef{.text = role, .size = 28, .color = {0.6f, 0.6f, 0.7f, 1}, .align = TextAlign::Center, .tracking = 4}, {W * 0.4f, 40}, {-200, 0, 0});
        apply_text(l, "name", TextDef{.text = name, .size = 36, .color = {1, 1, 1, 1}, .align = TextAlign::Center, .tracking = 1}, {W * 0.4f, 40}, {200, 0, 0});
    }
};

struct SequenceItem {
    std::string text;
    f32 start;
    f32 duration;
    f32 size{72.0f};

    SequenceItem(std::string t) : text(std::move(t)) {}
    SequenceItem& set_timing(f32 s, f32 d) { start = s; duration = d; return *this; }
    SequenceItem& set_size(f32 s) { size = s; return *this; }

    [[nodiscard]] f32 opacity(Frame frame) const {
        f32 local = static_cast<f32>(frame) - start;
        if (local < 0 || local >= duration) return 0.0f;
        f32 fade = 10.0f;
        if (local < fade) return local / fade;
        if (local > duration - fade) return (duration - local) / fade;
        return 1.0f;
    }

    void draw(LayerBuilder& l, Frame frame) const {
        l.opacity(opacity(frame)).pin_to(Anchor::Center);
        apply_text(l, "text", TextDef{
            .text = text,
            .size = size,
            .color = {1, 1, 1, 1},
            .align = TextAlign::Center,
            .tracking = 6.0f,
        }, {W * 0.8f, 140.0f});
    }
};

} // namespace

Composition text_credit_roll() {
    return composition({.name = "TextCreditRoll", .duration = 300}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 scroll_y = ctx.progress() * 1200.0f + H;
        
        s.layer("bg", [](auto& l) { l.fill({0.005f, 0.008f, 0.020f, 1.0f}); });
        s.layer("credits_title", [&](auto& l) {
            l.position({0, scroll_y - 200, 0});
            apply_text(l, "title", TextDef{.text="C R E D I T S", .size=56, .color={0.25f, 0.52f, 1, 1}, .align=TextAlign::Center, .tracking=14}, {W*0.6f, 80});
        });

        static const std::vector<CreditItem> credits = {
            {"Direction", "Pierone"}, {"Engine", "Chronon3D Team"}, {"Rendering", "Software Backend"},
            {"Animation", "Motion Presets"}, {"Typography", "Inter Typeface"}, {"Pipeline", "Content Generator"},
            {"Testing", "Automated Suite"}, {"Tools", "CLI Application"}
        };

        for (size_t i = 0; i < credits.size(); ++i) {
            s.layer("item_" + std::to_string(i), [&](auto& l) {
                credits[i].draw(l, scroll_y - 320.0f - static_cast<f32>(i) * 110.0f);
            });
        }
        return s.build();
    });
}

Composition text_animated_sequence() {
    return composition({.name = "TextAnimatedSequence", .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("bg", [](auto& l) { l.fill({0.008f, 0.012f, 0.025f, 1.0f}); });

        static const std::vector<SequenceItem> items = {
            SequenceItem("PART ONE").set_timing(0, 50).set_size(80),
            SequenceItem("The Journey Begins").set_timing(55, 50).set_size(56),
            SequenceItem("PART TWO").set_timing(110, 55).set_size(80),
            SequenceItem("Resolution").set_timing(170, 10).set_size(56)
        };

        for (const auto& item : items) {
            if (item.opacity(ctx.frame) > 0) {
                s.layer("seq_" + item.text, [&](auto& l) { item.draw(l, ctx.frame); });
            }
        }
        return s.build();
    });
}

Composition text_countdown() {
    return composition({.name = "TextCountdown", .duration = 30}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        i32 num = std::max(1, static_cast<i32>(std::ceil(3.0f - static_cast<f32>(ctx.frame) / 10.0f)));
        
        s.layer("bg", [](auto& l) { l.fill({0.02f, 0.02f, 0.04f, 1.0f}); });
        s.layer("count_num", [&](auto& l) {
            l.opacity(0.9f + 0.1f * std::sin(static_cast<f32>(ctx.frame) * 0.5f)).pin_to(Anchor::Center);
            apply_text(l, "num", TextDef{.text=std::to_string(num), .size=220, .align=TextAlign::Center}, {W*0.5f, 300});
        });
        return s.build();
    });
}

} // namespace chronon3d::content::text
