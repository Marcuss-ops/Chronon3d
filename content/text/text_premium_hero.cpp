#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "text_helpers.hpp"

namespace chronon3d::content::text {

namespace {

TextParams make_hero_title(const std::string& title) {
    TextParams p;
    p.text = title;
    p.size = {1360.0f, 210.0f};
    p.pos = {0.0f, -86.0f, 0.0f};
    p.font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    p.font_family = "DejaVu Sans";
    p.font_weight = 900;
    p.font_style = "normal";
    p.font_size = 118.0f;
    p.color = {0.98f, 0.16f, 0.86f, 1.0f};
    p.align = TextAlign::Center;
    p.vertical_align = VerticalAlign::Middle;
    p.line_height = 1.0f;
    p.tracking = -2.0f;
    p.box_style = TextBoxStyle{};
    return p;
}

TextParams make_hero_subtitle(const std::string& text) {
    TextParams p;
    p.text = text;
    p.size = {1020.0f, 68.0f};
    p.pos = {0.0f, 86.0f, 0.0f};
    p.font_path = "assets/fonts/Inter-Regular.ttf";
    p.font_family = "Inter";
    p.font_weight = 400;
    p.font_style = "normal";
    p.font_size = 28.0f;
    p.color = Color{0.84f, 0.88f, 0.95f, 1.0f};
    p.align = TextAlign::Center;
    p.vertical_align = VerticalAlign::Middle;
    p.line_height = 1.2f;
    p.tracking = 1.4f;
    return p;
}

} // namespace

Composition text_premium_hero() {
    return composition({.name = "TextPremiumHero", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("bg", [](auto& l) {
            l.fill({0.015f, 0.016f, 0.024f, 1.0f});
        });

        s.layer("hero_text", [](auto& l) {
            l.text("title", make_hero_title("BUTTERY SMOOTH"));
        });

        return s.build();
    });
}

Composition text_hero_fresh() {
    // Hero text "BUTTERY SMOOTH" on a transparent background — no fill() / bg.
    return composition({.name = "HeroFresh", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("hero_text", [](auto& l) {
            l.text("title", make_hero_title("BUTTERY SMOOTH"));
        });

        return s.build();
    });
}

Composition text_empty() {
    // Empty scene with no layers — tests that the renderer outputs a
    // fully-transparent (or black) frame without crashing or clipping.
    return composition({.name = "Empty", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        return s.build();
    });
}

Composition text_only() {
    // Centered text with NO background fill — the text should render on a
    // transparent canvas. This verifies that text positioning is correct
    // independently of any fullscreen_rect / fill() transform.
    return composition({.name = "TextOnly", .width = 1920, .height = 1080, .duration = 1}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.layer("title", [](auto& l) {
            l.pin_to(Anchor::Center);
            l.text("main", TextParams{
                .text = "CIAO MONDO",
                .size = {1000.0f, 160.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                .font_family = "DejaVu Sans",
                .font_weight = 900,
                .font_style = "normal",
                .font_size = 96.0f,
                .color = {0.98f, 0.16f, 0.86f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .line_height = 1.0f,
                .tracking = 0.0f,
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text

CHRONON_REGISTER_COMPOSITION("TextPremiumHero", chronon3d::content::text::text_premium_hero)
