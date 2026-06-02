#include "../common/glow_test_common.hpp"
#include <chronon3d/layout/design_kit.hpp>
#include <chronon3d/presets/text/text_style_presets.hpp>
#include "content/text/helpers/text_helpers.hpp"

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
    using chronon3d::content::text::make_text_params;

    s.layer("hero_text", [&](LayerBuilder& l) {
        l.position({0.0f, -14.0f, 0.0f});
        l.glow(42.0f, 1.25f, glow_color);
        l.drop_shadow({0.0f, 14.0f}, shadow_color, 26.0f);
        auto hero_style = presets::text::premium_hero_title();
        hero_style.paint.fill_style = fill;
        hero_style.paint.stroke_color = Color{0.0f, 0.0f, 0.0f, 0.20f};
        hero_style.material.top_color = {1.0f, 1.0f, 1.0f, 1.0f};
        hero_style.material.bottom_color = {0.92f, 0.70f, 1.0f, 1.0f};
        l.text("hero", make_text_params(title, hero_style, {1240.0f, 190.0f}, {0.0f, 0.0f, 0.0f}));

        auto subtitle_style = presets::text::premium_subtitle();
        subtitle_style.size = 28.0f;
        subtitle_style.tracking = 1.2f;
        subtitle_style.paint.fill_style = std::nullopt;
        subtitle_style.paint.stroke_enabled = false;
        subtitle_style.material = TextMaterial::glass();
        subtitle_style.material.top_color = {0.92f, 0.94f, 0.98f, 1.0f};
        subtitle_style.material.bottom_color = {0.80f, 0.86f, 0.95f, 1.0f};
        l.text("sub", make_text_params(subtitle, subtitle_style, {920.0f, 54.0f}, {0.0f, 108.0f, 0.0f}));
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
                .grid_color = Color{0.32f, 0.10f, 0.42f, 0.045f}, // Subtle dark purple grid lines
                .spacing = 36.0f,
                .minor_thickness = 0.5f,
                .major_thickness = 0.8f,
                .major_every = 4,
                .centered = true
            });
        });

        // 2. Transparent Capsule Outline
        s.layer("capsule", [](LayerBuilder& l) {
            l.position({-130.0f, -110.0f, 0.0f});
            l.glow(16.0f, 0.25f, Color{0.95f, 0.12f, 0.58f, 0.30f});
            draw_premium_pill(l, "pill", PremiumPillStyle{
                .size = {930.0f, 120.0f},
                .radius = 60.0f,
                .fill = Color{0.03f, 0.01f, 0.05f, 0.14f},
                .stroke = Color{0.78f, 0.48f, 0.88f, 0.28f},
                .stroke_width = 1.0f
            });
        });

        // 3. Side-by-side text layout using RichTextLine
        s.layer("text", [](LayerBuilder& l) {
            l.position({-130.0f, -110.0f, 0.0f});

            RichTextLine rtl;
            rtl.run("Buttery", Color{1.0f, 0.0f, 0.78f, 1.0f}, 72.0f)
               .space(24.0f)
               .run("Smooth", Color::white(), 72.0f)
               .space(24.0f)
               .star(Color{1.0f, 0.0f, 0.78f, 1.0f}, 42.0f, 12.0f, 8);

            draw_rich_text(
                l,
                rtl,
                {0.0f, 0.0f, 0.0f},
                RichTextLayoutOptions{
                    .origin = {0.0f, 0.0f, 0.0f},
                    .anchor = RichTextAnchor::Center,
                    .vertical_anchor = RichTextVerticalAnchor::Middle,
                    .glyph_padding = 6.0f,
                    .snap_to_pixels = true
                },
                l.font_engine(),
                "hero_phrase"
            );
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
            TextMaterial mat = TextMaterial::premium();
            mat.top_color               = {1.0f, 1.0f, 1.0f, 1.0f};
            mat.bottom_color            = {0.52f, 0.78f, 1.0f, 1.0f};
            mat.bevel_px                = 2.8f;
            mat.bevel_highlight_opacity = 0.55f;
            mat.bevel_highlight_color   = {1.0f, 1.0f, 1.0f, 1.0f};
            mat.bevel_shadow_opacity    = 0.40f;
            mat.top_highlight_opacity   = 0.35f;
            mat.top_highlight_fraction   = 0.12f;
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
            
            auto sub_style = presets::text::premium_subtitle();
            sub_style.size = 36.0f;
            sub_style.tracking = 16.0f;
            sub_style.color = Color{0.85f, 0.93f, 1.0f, 1.0f};
            sub_style.paint.fill = sub_style.color;
            sub_style.paint.fill_style = std::nullopt;
            sub_style.paint.stroke_enabled = false;
            sub_style.material = TextMaterial::glass();
            sub_style.material.top_color = {0.95f, 0.98f, 1.0f, 1.0f};
            sub_style.material.bottom_color = {0.75f, 0.88f, 1.0f, 1.0f};
            l.text("sub", chronon3d::content::text::make_text_params("FULL TUTORIAL", sub_style, {1240.0f, 60.0f}, {0.0f, 160.0f, 0.0f}));
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
