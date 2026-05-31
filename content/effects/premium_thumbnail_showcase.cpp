#include "glow_test_common.hpp"

namespace chronon3d::content::effects {

namespace {

void add_sparkles(SceneBuilder& s, Color c) {
    s.layer("sparkles", [=](LayerBuilder& l) {
        l.opacity(0.85f);
        l.circle("s1", {.radius = 7.0f, .color = c, .pos = {-420.0f, -120.0f, 0.0f}});
        l.circle("s2", {.radius = 4.0f, .color = c, .pos = {430.0f, -95.0f, 0.0f}});
        l.circle("s3", {.radius = 3.0f, .color = c, .pos = {380.0f, 115.0f, 0.0f}});
    });
}

void add_hero_text(
    SceneBuilder& s,
    const std::string& title,
    const std::string& subtitle,
    const Fill& fill,
    Color glow_color,
    Color shadow_color
) {
    s.layer("hero_text", [&](LayerBuilder& l) {
        l.position({0.0f, -14.0f, 0.0f});
        l.glow(42.0f, 1.25f, glow_color);
        l.drop_shadow({0.0f, 14.0f}, shadow_color, 26.0f);
        l.text("hero", {
            .text = title,
            .size = {1240.0f, 190.0f},
            .pos = {0.0f, 0.0f, 0.0f},
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .font_family = "Inter",
            .font_weight = 800,
            .font_style = "normal",
            .font_size = 108.0f,
            .color = Color::white(),
            .align = TextAlign::Center,
            .vertical_align = VerticalAlign::Middle,
            .line_height = 1.2f,
            .tracking = -1.0f,
            .paint = {
                .fill_style = fill,
                .stroke_enabled = true,
                .stroke_color = Color{0.0f, 0.0f, 0.0f, 0.16f},
                .stroke_width = 1.1f
            }
        });
        l.text("sub", {
            .text = subtitle,
            .size = {920.0f, 54.0f},
            .pos = {0.0f, 108.0f, 0.0f},
            .font_path = "assets/fonts/Inter-Regular.ttf",
            .font_family = "Inter",
            .font_weight = 400,
            .font_style = "normal",
            .font_size = 28.0f,
            .color = Color{0.92f, 0.94f, 0.98f, 1.0f},
            .align = TextAlign::Center,
            .vertical_align = VerticalAlign::Middle,
            .line_height = 1.2f,
            .tracking = 1.2f
        });
    });
}

} // namespace

Composition premium_thumbnail_buttery_smooth() {
    return composition({.name = "PremiumThumbnailButterySmooth", .width = kW, .height = kH, .duration = 1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.025f, 0.010f, 0.035f, 1.0f}, Color{0.100f, 0.020f, 0.110f, 1.0f});

        s.layer("panel", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("card", {
                .size = {1280.0f, 430.0f},
                .radius = 38.0f,
                .color = Color{0.02f, 0.02f, 0.03f, 0.82f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, Color{0.04f, 0.04f, 0.05f, 0.92f}},
                        {1.0f, Color{0.10f, 0.03f, 0.12f, 0.86f}},
                    }
                )
            });
            l.drop_shadow({0.0f, 26.0f}, Color{0.0f, 0.0f, 0.0f, 0.62f}, 42.0f);
            l.glow(28.0f, 0.75f, Color{0.90f, 0.18f, 0.92f, 1.0f});
        });

        add_hero_text(
            s,
            "Buttery Smooth",
            "Motion design quality text with soft glow and sharp core",
            Fill::linear(
                {0.0f, 0.42f},
                {1.0f, 0.58f},
                {
                    {0.0f, Color{0.95f, 0.18f, 0.95f, 1.0f}},
                    {0.48f, Color{0.98f, 0.58f, 0.90f, 1.0f}},
                    {1.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}},
                }
            ),
            Color{0.95f, 0.18f, 0.95f, 1.0f},
            Color{0.0f, 0.0f, 0.0f, 0.55f}
        );

        add_sparkles(s, Color{0.95f, 0.18f, 0.95f, 1.0f});
        bottom_label(s, "Premium thumbnail base: buttery smooth");
        return s.build();
    });
}

Composition premium_thumbnail_saas_blue() {
    return composition({.name = "PremiumThumbnailSaaSBlue", .width = kW, .height = kH, .duration = 1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.015f, 0.050f, 0.160f, 1.0f}, Color{0.020f, 0.120f, 0.260f, 1.0f});

        s.layer("arc", [](LayerBuilder& l) {
            l.position({0.0f, 250.0f, 0.0f});
            l.glow(30.0f, 1.0f, Color{0.12f, 0.60f, 1.0f, 1.0f});
            l.rounded_rect("curve", {
                .size = {1100.0f, 220.0f},
                .radius = 110.0f,
                .color = Color{0.10f, 0.55f, 1.0f, 0.18f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, Color{0.08f, 0.45f, 1.0f, 0.12f}},
                        {1.0f, Color{0.20f, 0.95f, 1.0f, 0.24f}},
                    }
                )
            });
        });

        add_hero_text(
            s,
            "SaaS",
            "FULL TUTORIAL",
            Fill::linear(
                {0.0f, 0.40f},
                {1.0f, 0.60f},
                {
                    {0.0f, Color{0.98f, 0.99f, 1.0f, 1.0f}},
                    {0.55f, Color{0.55f, 0.83f, 1.0f, 1.0f}},
                    {1.0f, Color{0.20f, 0.45f, 0.95f, 1.0f}},
                }
            ),
            Color{0.12f, 0.55f, 1.0f, 1.0f},
            Color{0.0f, 0.12f, 0.32f, 0.70f}
        );

        s.layer("badge", [](LayerBuilder& l) {
            l.position({-470.0f, -250.0f, 0.0f});
            l.rounded_rect("ae", {
                .size = {110.0f, 110.0f},
                .radius = 24.0f,
                .color = Color{0.94f, 0.97f, 1.0f, 0.96f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, Color{0.95f, 0.98f, 1.0f, 0.98f}},
                        {1.0f, Color{0.55f, 0.84f, 1.0f, 0.96f}},
                    }
                )
            });
            l.text("ae_text", {
                .text = "Ae",
                .size = {110.0f, 110.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_style = "normal",
                .font_size = 40.0f,
                .color = Color{0.10f, 0.12f, 0.18f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .line_height = 1.2f,
                .tracking = 0.0f
            });
        });

        add_sparkles(s, Color{0.55f, 0.83f, 1.0f, 1.0f});
        bottom_label(s, "Premium thumbnail base: SaaS blue");
        return s.build();
    });
}

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("PremiumThumbnailButterySmooth", chronon3d::content::effects::premium_thumbnail_buttery_smooth)
CHRONON_REGISTER_COMPOSITION("PremiumThumbnailSaaSBlue", chronon3d::content::effects::premium_thumbnail_saas_blue)
