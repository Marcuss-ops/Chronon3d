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

        // 1. Background layer: Dark with radial glow and subtle purple grid
        s.layer("background", [](LayerBuilder& l) {
            l.rect("bg", {
                .size = {(f32)kW, (f32)kH},
                .color = Color{0.02f, 0.006f, 0.035f, 1.0f},
            });
            
            l.rect("bg_glow", {
                .size = {(f32)kW, (f32)kH},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::radial(
                    {0.82f, 0.50f}, // Spotlight glow on the right
                    0.80f,
                    {
                        {0.0f, Color{0.18f, 0.04f, 0.28f, 0.45f}},
                        {0.65f, Color{0.02f, 0.006f, 0.035f, 0.15f}},
                        {1.0f, Color{0.002f, 0.0f, 0.005f, 0.90f}} // Vignette edges
                    }
                )
            });

            l.grid_background("grid", {
                .size = {(f32)kW, (f32)kH},
                .bg_color = {0.0f, 0.0f, 0.0f, 0.0f},
                .grid_color = Color{0.32f, 0.10f, 0.42f, 0.09f}, // Subtle dark purple grid lines
                .spacing = 36.0f,
                .minor_thickness = 0.9f,
                .major_thickness = 1.3f,
                .major_every = 4,
                .centered = true
            });
        });

        // 2. Transparent Capsule Outline
        s.layer("capsule", [](LayerBuilder& l) {
            l.position({-30.0f, 0.0f, 0.0f});
            l.glow(26.0f, 0.70f, Color{0.86f, 0.38f, 0.92f, 0.28f});
            l.drop_shadow({0.0f, 14.0f}, Color{0.0f, 0.0f, 0.0f, 0.45f}, 24.0f);

            // Outer border card
            l.rounded_rect("pill_border", {
                .size = {1143.0f, 183.0f},
                .radius = 91.5f,
                .color = Color{0.82f, 0.58f, 0.90f, 0.45f}
            });

            // Inner filled card
            l.rounded_rect("pill_body", {
                .size = {1140.0f, 180.0f},
                .radius = 90.0f,
                .color = Color{0.04f, 0.015f, 0.07f, 0.65f},
                .fill = Fill::linear(
                    {-0.5f, 0.0f}, {0.5f, 0.0f},
                    {
                        {0.0f, Color{0.03f, 0.01f, 0.05f, 0.75f}},
                        {1.0f, Color{0.09f, 0.02f, 0.14f, 0.40f}}
                    }
                )
            });
        });

        // 3. Side-by-side text layout: "Buttery Smooth" + glowing star asterisk
        s.layer("text", [](LayerBuilder& l) {
            l.position({-50.0f, 0.0f, 0.0f});
            
            // "Buttery" (Magenta/pink)
            l.text("t1", {
                .text = "Buttery",
                .size = {500.0f, 160.0f},
                .pos = {-240.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 85.0f,
                .color = Color{0.95f, 0.12f, 0.58f, 1.0f},
                .align = TextAlign::Right,
                .vertical_align = VerticalAlign::Middle
            });

            // "Smooth" (White)
            l.text("t2", {
                .text = "Smooth",
                .size = {500.0f, 160.0f},
                .pos = {280.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 85.0f,
                .color = Color::white(),
                .align = TextAlign::Left,
                .vertical_align = VerticalAlign::Middle
            });
        });

        // 4. Glowing 8-Point Asterisk Star at the right of Smooth text
        s.layer("star_glow", [](LayerBuilder& l) {
            l.position({320.0f, 0.0f, 0.0f});
            l.glow(40.0f, 1.80f, Color{0.95f, 0.12f, 0.58f, 0.85f});
            
            l.star("star", {
                .center = {0.0f, 0.0f},
                .points = 8,
                .inner_radius = 9.0f,
                .outer_radius = 28.0f,
                .color = Color{0.95f, 0.12f, 0.58f, 1.0f}
            });
        });

        return s.build();
    });
}

Composition premium_thumbnail_saas_blue() {
    return composition({.name = "PremiumThumbnailSaaSBlue", .width = kW, .height = kH, .duration = 1},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        
        // 1. Fullscreen radial gradient background with vignette
        s.layer("background", [](LayerBuilder& l) {
            l.rect("bg", {
                .size = {(f32)kW, (f32)kH},
                .color = Color::white(),
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::radial(
                    {0.5f, 0.5f},
                    0.80f,
                    {
                        {0.0f, Color{0.015f, 0.18f, 0.48f, 1.0f}}, // Bright cyan-blue center spotlight
                        {0.55f, Color{0.003f, 0.035f, 0.12f, 1.0f}}, // Dark blue
                        {1.0f, Color{0.001f, 0.004f, 0.02f, 1.0f}} // Vignette corners
                    }
                )
            });
        });

        // 2. Volumetric Triangle Light Beam
        s.layer("light_beam", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            l.opacity(0.85f);
            
            PathParams p;
            p.commands = {
                PathCommand::move_to({0.0f, -512.0f}),
                PathCommand::line_to({-768.0f, 512.0f}),
                PathCommand::line_to({768.0f, 512.0f}),
                PathCommand::close()
            };
            p.fill = Fill::linear(
                {0.5f, 0.0f},
                {0.5f, 1.0f},
                {
                    {0.0f, Color{0.0f, 0.85f, 1.0f, 0.35f}},
                    {1.0f, Color{0.005f, 0.040f, 0.180f, 0.00f}}
                }
            );
            l.path("beam", p);
        });

        // 3. Upgraded centered "Ae" Branding Badge (directly above SaaS)
        s.layer("badge", [](LayerBuilder& l) {
            l.position({0.0f, -250.0f, 0.0f});
            l.glow(20.0f, 0.80f, Color{0.0f, 0.80f, 1.0f, 0.60f});
            l.drop_shadow({0.0f, 10.0f}, Color{0.0f, 0.0f, 0.0f, 0.50f}, 14.0f);
            
            // Outer border card
            l.rounded_rect("ae_card_border", {
                .size = {1280.0f * 0.1f, 1280.0f * 0.1f}, // Slightly scaled down badge
                .radius = 26.0f,
                .color = Color{0.0f, 0.85f, 1.0f, 0.85f}
            });

            // Inner filled card
            l.rounded_rect("ae_card", {
                .size = {120.0f, 120.0f},
                .radius = 23.0f,
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
                .size = {120.0f, 120.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 48.0f,
                .color = Color{0.0f, 0.85f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle
            });
        });

        // 4. Star Sparkles/Glints (Aesthetic template glints left and right)
        s.layer("sparkles", [](LayerBuilder& l) {
            l.glow(18.0f, 0.95f, Color::white());
            
            l.star("star_left", {
                .center = {-340.0f, -50.0f},
                .points = 4,
                .inner_radius = 8.0f,
                .outer_radius = 35.0f,
                .color = Color::white()
            });
            
            l.star("star_right", {
                .center = {340.0f, 50.0f},
                .points = 4,
                .inner_radius = 10.0f,
                .outer_radius = 45.0f,
                .color = Color::white()
            });
        });

        // 5. Giant SaaS Title with Premium TextMaterial + Wide Subtitle (moved up)
        s.layer("hero_text", [](LayerBuilder& l) {
            l.position({0.0f, -20.0f, 0.0f}); // Moved up in layout
            
            // Build the customized C++ TextMaterial for the lucido premium look
            TextMaterial mat;
            mat.enabled = true;
            mat.top_color               = {1.0f, 1.0f, 1.0f, 1.0f};
            mat.bottom_color            = {0.52f, 0.78f, 1.0f, 1.0f}; // gradient bianco azzurro
            mat.bevel_px                = 2.8f; // fake 3D edge depth
            mat.bevel_highlight_opacity = 0.55f;
            mat.bevel_highlight_color   = {1.0f, 1.0f, 1.0f, 1.0f}; // highlight bianco sopra
            mat.bevel_shadow_opacity    = 0.40f;
            mat.top_highlight_opacity   = 0.35f;
            mat.top_highlight_fraction  = 0.12f;
            mat.bottom_shade_opacity    = 0.20f;
            mat.emissive                = 1.15f;
            mat.use_material_glow       = true;
            mat.glow_radius             = 40.0f;
            mat.glow_intensity          = 1.25f;
            mat.glow_color              = {0.0f, 0.65f, 1.0f, 0.80f};
            mat.use_material_shadow     = true;
            mat.shadow_offset           = {0.0f, 16.0f};
            mat.shadow_blur             = 26.0f;
            mat.shadow_opacity          = 0.85f;
            mat.shadow_color            = {0.0f, 0.02f, 0.12f, 0.95f};

            l.text("hero", {
                .text = "SaaS",
                .size = {1500.0f, 320.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 250.0f, // Huge size!
                .color = Color::white(),
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = -2.0f,
                .paint = {
                    .fill_style = std::nullopt, // Handled by TextMaterial instead
                    .stroke_enabled = true,
                    .stroke_color = Color{0.0f, 0.04f, 0.16f, 0.90f}, // stroke blu scuro
                    .stroke_width = 4.0f
                },
                .material = mat, // Natively apply the TextMaterial
                .wrap = TextWrap::None
            });
            
            l.text("sub", {
                .text = "FULL TUTORIAL",
                .size = {1240.0f, 60.0f},
                .pos = {0.0f, 160.0f, 0.0f}, // Placed centered under SaaS
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 36.0f,
                .color = Color{0.85f, 0.93f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 16.0f, // Elegant tracking letter spacing
                .shadows = {
                    TextShadow{
                        .enabled = true,
                        .offset = {0.0f, 6.0f},
                        .blur = 10.0f,
                        .opacity = 0.65f,
                        .color = {0.0f, 0.02f, 0.12f, 1.0f}
                    }
                }
            });
        });

        // 6. Thin Glowing Curved Horizon Line (Bottom)
        s.layer("arc", [](LayerBuilder& l) {
            l.position({0.0f, 512.0f, 0.0f}); // Placed at the bottom edge
            l.glow(35.0f, 1.6f, Color{0.0f, 0.85f, 1.0f, 1.0f});
            
            PathParams p;
            p.commands = {
                PathCommand::move_to({-768.0f, 0.0f}),
                PathCommand::quadratic_to({0.0f, -60.0f}, {768.0f, 0.0f}), // Elegant curve
                PathCommand::line_to({768.0f, 20.0f}),
                PathCommand::line_to({-768.0f, 20.0f}),
                PathCommand::close()
            };
            p.fill = Fill::linear(
                {0.5f, 0.0f},
                {0.5f, 1.0f},
                {
                    {0.0f, Color{0.0f, 0.90f, 1.0f, 0.75f}},
                    {1.0f, Color{0.00f, 0.35f, 1.0f, 0.00f}}
                }
            );
            l.path("glow_curve", p);
        });

        return s.build();
    });
}

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("PremiumThumbnailButterySmooth", chronon3d::content::effects::premium_thumbnail_buttery_smooth)
CHRONON_REGISTER_COMPOSITION("PremiumThumbnailSaaSBlue", chronon3d::content::effects::premium_thumbnail_saas_blue)
