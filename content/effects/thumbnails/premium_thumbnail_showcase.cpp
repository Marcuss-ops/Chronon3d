#include "../common/glow_test_common.hpp"

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
        
        // 1. Deep blue gradient background
        deep_bg(s, Color{0.004f, 0.012f, 0.040f, 1.0f}, Color{0.008f, 0.028f, 0.090f, 1.0f});

        // 2. Volumetric Triangle Light Beam from top center downward
        s.layer("light_beam", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            l.opacity(0.88f);
            
            PathParams p;
            p.commands = {
                PathCommand::move_to({0.0f, -512.0f}), // Top center
                PathCommand::line_to({-768.0f, 512.0f}), // Bottom left
                PathCommand::line_to({768.0f, 512.0f}), // Bottom right
                PathCommand::close()
            };
            p.fill = Fill::linear(
                {0.5f, 0.0f},
                {0.5f, 1.0f},
                {
                    {0.0f, Color{0.0f, 0.82f, 1.0f, 0.38f}}, // Volumetric Cyan transparent at the top
                    {1.0f, Color{0.008f, 0.050f, 0.220f, 0.00f}} // Fades out completely at the bottom
                }
            );
            l.path("beam", p);
        });

        // 3. Central Eye Vector Graphic at the top center of the light beam
        s.layer("eye_graphic", [](LayerBuilder& l) {
            l.position({0.0f, -320.0f, 0.0f});
            l.glow(24.0f, 0.85f, Color{0.0f, 0.8f, 1.0f, 0.40f});
            
            // Outer white sclera (stylized eye shape)
            PathParams eye_sclera;
            eye_sclera.commands = {
                PathCommand::move_to({-180.0f, 0.0f}),
                PathCommand::cubic_to({-90.0f, -100.0f}, {90.0f, -100.0f}, {180.0f, 0.0f}),
                PathCommand::cubic_to({90.0f, 100.0f}, {-90.0f, 100.0f}, {-180.0f, 0.0f}),
                PathCommand::close()
            };
            eye_sclera.fill = Fill::solid_color(Color::white());
            l.path("sclera", eye_sclera);
            
            // Inner pupil (black circle)
            l.circle("pupil", {
                .radius = 60.0f,
                .color = Color::black(),
                .pos = {0.0f, 0.0f, 0.0f}
            });
            
            // Iris shine (white circle)
            l.circle("shine", {
                .radius = 14.0f,
                .color = Color::white(),
                .pos = {18.0f, -18.0f, 0.0f}
            });
        });

        // 4. Upgraded "Ae" Branding Badge (Top Left)
        s.layer("badge", [](LayerBuilder& l) {
            l.position({-530.0f, -340.0f, 0.0f});
            l.glow(22.0f, 0.70f, Color{0.0f, 0.78f, 1.0f, 0.50f});
            l.drop_shadow({0.0f, 12.0f}, Color{0.0f, 0.0f, 0.0f, 0.45f}, 16.0f);
            
            // Outer border card
            l.rounded_rect("ae_card_border", {
                .size = {146.0f, 146.0f},
                .radius = 34.0f,
                .color = Color{0.0f, 0.82f, 1.0f, 0.85f}
            });

            // Inner filled card
            l.rounded_rect("ae_card", {
                .size = {138.0f, 138.0f},
                .radius = 30.0f,
                .color = Color::white(),
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, Color{0.08f, 0.12f, 0.32f, 1.0f}},
                        {1.0f, Color{0.01f, 0.02f, 0.08f, 1.0f}},
                    }
                )
            });
            
            l.text("ae_text", {
                .text = "Ae",
                .size = {140.0f, 140.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 56.0f,
                .color = Color{0.0f, 0.82f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle
            });
        });

        // 5. Four-pointed Star Sparkles / Glints (matching After Effects shine template)
        s.layer("sparkles", [](LayerBuilder& l) {
            l.glow(18.0f, 0.95f, Color::white());
            
            l.star("star1", {
                .center = {420.0f, -280.0f},
                .points = 4,
                .inner_radius = 9.0f,
                .outer_radius = 45.0f,
                .color = Color::white()
            });
            
            l.star("star2", {
                .center = {-360.0f, -180.0f},
                .points = 4,
                .inner_radius = 6.0f,
                .outer_radius = 28.0f,
                .color = Color::white()
            });
        });

        // 6. Premium Center SaaS Title + Wide Subtitle
        s.layer("hero_text", [](LayerBuilder& l) {
            l.position({0.0f, 60.0f, 0.0f});
            l.glow(45.0f, 1.35f, Color{0.12f, 0.60f, 1.0f, 1.0f});
            l.drop_shadow({0.0f, 16.0f}, Color{0.0f, 0.05f, 0.20f, 0.85f}, 28.0f);
            
            l.text("hero", {
                .text = "SaaS",
                .size = {1500.0f, 300.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 180.0f,
                .color = Color::white(),
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = -1.0f,
                .paint = {
                    .fill_style = Fill::linear(
                        {0.5f, 0.0f},
                        {0.5f, 1.0f},
                        {
                            {0.0f, Color{1.0f, 1.0f, 1.0f, 1.0f}},
                            {0.52f, Color{0.68f, 0.88f, 1.0f, 1.0f}},
                            {1.0f, Color{0.25f, 0.52f, 0.95f, 1.0f}},
                        }
                    ),
                    .stroke_enabled = true,
                    .stroke_color = Color{0.0f, 0.05f, 0.18f, 0.85f},
                    .stroke_width = 4.5f
                },
                .wrap = TextWrap::None
            });
            
            l.text("sub", {
                .text = "FULL TUTORIAL",
                .size = {1240.0f, 60.0f},
                .pos = {0.0f, 150.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 700,
                .font_size = 38.0f,
                .color = Color{0.88f, 0.94f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 14.0f // Beautiful wide tracking
            });
        });

        // 7. Bright Curved Neon Horizon Line (Bottom)
        s.layer("arc", [](LayerBuilder& l) {
            l.position({0.0f, 480.0f, 0.0f});
            l.glow(40.0f, 1.5f, Color{0.0f, 0.80f, 1.0f, 1.0f});
            l.rounded_rect("curve", {
                .size = {1600.0f, 160.0f},
                .radius = 80.0f,
                .color = Color{0.0f, 0.80f, 1.0f, 0.35f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, Color{0.00f, 0.35f, 1.0f, 0.10f}},
                        {0.5f, Color{0.00f, 0.90f, 1.0f, 0.65f}},
                        {1.0f, Color{0.00f, 0.35f, 1.0f, 0.10f}},
                    }
                )
            });
        });

        bottom_label(s, "Premium thumbnail base: SaaS blue");
        return s.build();
    });
}

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("PremiumThumbnailButterySmooth", chronon3d::content::effects::premium_thumbnail_buttery_smooth)
CHRONON_REGISTER_COMPOSITION("PremiumThumbnailSaaSBlue", chronon3d::content::effects::premium_thumbnail_saas_blue)
