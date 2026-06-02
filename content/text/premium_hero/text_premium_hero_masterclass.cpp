#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/layout/design_kit.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include "component_library.hpp"

namespace chronon3d::content::text {

// ── PastelMasterclass template ─────────────────────────────────────────────────
Composition text_premium_hero() {
    using namespace style;
    using namespace style::component;

    const auto cfg = pastel_masterclass();

    return composition({.name = "TextPremiumHero", .width = 1920, .height = 1080, .duration = 1}, [cfg](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        add_background(s, cfg);
        add_corner_shapes(s, cfg);
        add_app_badge(s, cfg, "Ae");
        add_hero_title(s, cfg, "SaaS");
        add_subtitle(s, cfg, "Masterclass");
        add_gradient_button(s, cfg, "PART 1");

        // Decorative pink dot (specific to this variant)
        s.layer("dot", [](LayerBuilder& l) {
            l.position({740.0f, -340.0f, 0.0f});
            l.circle("pink", {.radius = 36.0f, .color = {0.98f, 0.45f, 0.60f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}});
            l.circle("halo", {.radius = 52.0f, .color = {0.98f, 0.45f, 0.60f, 0.12f}, .pos = {0.0f, 0.0f, 0.0f}});
        });

        return s.build();
    });
}

// ── DarkSaaSNeon template (SaaS Blue variant) ──────────────────────────────────
Composition text_premium_hero_saas_blue() {
    using namespace style;
    using namespace style::component;

    const auto cfg = dark_saas_neon();

    return composition({.name = "TextPremiumHeroSaaSBlue", .width = 1920, .height = 1080, .duration = 1}, [cfg](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        add_background(s, cfg);
        add_ambient_lighting(s, cfg);
        add_ambient_arc(s, cfg);
        add_app_badge(s, cfg, "Ae");
        add_hero_title(s, cfg, "SaaS",
            Vec3{0.0f, -52.0f, 0.0f},  // override hero pos
            Vec2{900.0f, 200.0f}       // override hero box
        );
        add_subtitle(s, cfg, "FULL TUTORIAL",
            Vec3{0.0f, 130.0f, 0.0f},  // override subtitle pos
            Vec2{800.0f, 60.0f}        // override subtitle box
        );

        // Hi-layer (subtle reflection)
        s.layer("hero_hi", [cfg](LayerBuilder& l) {
            l.opacity(0.17f);
            l.text("title_hi", premium::hero_text(
                "SaaS",
                {900.0f, 200.0f},
                {0.0f, -72.0f, 0.0f},
                128.0f,
                "assets/fonts/Inter-Bold.ttf",
                "Inter",
                {1.0f, 1.0f, 1.0f, 0.95f},
                Fill::linear({0.0f, 0.0f}, {0.0f, 1.0f}, {
                    {0.0f, {1.0f, 1.0f, 1.0f, 0.95f}},
                    {1.0f, {1.0f, 1.0f, 1.0f, 0.0f}},
                }),
                {0.0f, 0.0f, 0.0f, 0.0f},
                0.0f,
                -2.0f,
                VerticalAlign::Middle,
                cfg.hero_shadow()
            ));
        });

        add_floor_arc(s, cfg);
        add_sparkles(s, cfg, 16.0f, 720.0f);

        return s.build();
    });
}

// ── PinkExplainer template ─────────────────────────────────────────────────────
Composition text_premium_hero_explainer() {
    using namespace style;
    using namespace style::component;

    const auto cfg = pink_explainer();

    return composition({.name = "TextPremiumHeroExplainer", .width = 1920, .height = 1080, .duration = 1}, [cfg](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        add_background(s, cfg);

        // Decorative background shapes (specific to explainer)
        s.layer("top_left_wedge", [](LayerBuilder& l) {
            l.rotate({0.0f, 0.0f, -43.0f});
            l.rect("wedge", {
                .size = {860.0f, 160.0f},
                .color = {0.94f, 0.84f, 1.0f, 0.42f},
                .pos = {-770.0f, -485.0f, 0.0f},
                .fill = Fill::linear({0.0f, 0.0f}, {1.0f, 0.0f}, {
                    {0.0f, {0.90f, 0.82f, 1.0f, 0.38f}},
                    {1.0f, {0.98f, 0.72f, 0.96f, 0.32f}},
                }),
            });
        });
        s.layer("side_orb_left", [](LayerBuilder& l) {
            l.opacity(0.20f);
            l.circle("orb", {.radius = 180.0f, .color = {0.96f, 0.80f, 0.96f, 0.20f}, .pos = {-820.0f, -260.0f, 0.0f}});
        });
        s.layer("side_orb_right", [](LayerBuilder& l) {
            l.opacity(0.18f);
            l.circle("orb", {.radius = 160.0f, .color = {0.90f, 0.76f, 1.0f, 0.18f}, .pos = {820.0f, -250.0f, 0.0f}});
        });

        // Hero title with badge
        s.layer("title", [cfg](LayerBuilder& l) {
            l.position({0.0f, -336.0f, 0.0f});
            l.text("main", premium::hero_text(
                "SaaS Explainer",
                {1200.0f, 140.0f},
                {0.0f, 0.0f, 0.0f},
                96.0f,
                "assets/fonts/Inter-Bold.ttf",
                "Inter",
                {1.0f, 1.0f, 1.0f, 1.0f},
                Fill::linear({0.0f, 0.0f}, {1.0f, 0.0f}, {
                    {0.0f, {1.0f, 0.30f, 0.72f, 1.0f}},
                    {0.50f, {1.0f, 0.54f, 0.78f, 1.0f}},
                    {1.0f, {0.95f, 0.48f, 0.96f, 1.0f}},
                }),
                {1.0f, 1.0f, 1.0f, 0.05f},
                1.2f,
                -2.0f,
                VerticalAlign::Middle,
                cfg.hero_shadow()
            ));

        });

        // "masterclass" tag badge (positioned relative to title layer at y=-336)
        add_gradient_button(s, cfg, "masterclass", Vec3{520.0f, -288.0f, 0.0f}, Vec2{246.0f, 56.0f});

        // UI cards deck
        s.layer("deck", [](LayerBuilder& l) {
            l.position({0.0f, 158.0f, 0.0f});
            l.rounded_rect("back", {
                .size = {1360.0f, 720.0f}, .radius = 36.0f,
                .color = {0.96f, 0.95f, 0.99f, 0.82f}, .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear({0.0f, 0.0f}, {1.0f, 1.0f}, {
                    {0.0f, {1.0f, 1.0f, 1.0f, 0.86f}},
                    {1.0f, {0.95f, 0.95f, 1.0f, 0.78f}},
                }),
            });
            l.drop_shadow({0.0f, 24.0f}, {0.0f, 0.0f, 0.0f, 0.10f}, 58.0f);
        });

        // Cards grid (3 columns)
        s.layer("cards", [](LayerBuilder& l) {
            l.position({0.0f, 190.0f, 0.0f});
            l.rounded_rect("left",   {.size={300.0f,320.0f}, .radius=30.0f, .color={1,1,1,0.95f}, .pos={-380.0f,0,0}});
            l.rounded_rect("center", {.size={460.0f,380.0f}, .radius=34.0f, .color={1,1,1,0.96f}, .pos={0,-10.0f,0}});
            l.rounded_rect("right",  {.size={280.0f,310.0f}, .radius=28.0f, .color={1,1,1,0.93f}, .pos={390.0f,0,0}});
        });

        // Card labels
        s.layer("card_titles", [cfg](LayerBuilder& l) {
            l.position({0.0f, 80.0f, 0.0f});
            l.text("quick", premium::subtitle_text("Quick Notes",  {330.0f,60.0f}, {-350.0f,-130.0f,0}, 36.0f, {0.96f,0.50f,0.80f,1}, 0.4f));
            l.text("notes", premium::subtitle_text("documents", {330.0f,60.0f}, {390.0f,-130.0f,0}, 32.0f, {0.96f,0.50f,0.80f,0.90f}, 0.4f));
        });

        // Notes body text
        s.layer("notes", [](LayerBuilder& l) {
            l.position({0.0f, 202.0f, 0.0f});
            l.rounded_rect("note", {
                .size={410.0f,500.0f}, .radius=30.0f, .color={1,1,1,0.98f}, .pos={0,0,0},
                .fill=Fill::linear({0,0},{1,1},{{0,{1,1,1,1}},{1,{0.97f,0.97f,1,0.96f}}}),
            });
            l.text("body", {
                .text = "Every idea starts small. A place for creators, writers and thinkers to shape notes, scripts and systems into something publishable.",
                .size = {332.0f, 360.0f}, .pos = {0.0f, 18.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Regular.ttf", .font_family = "Inter",
                .font_weight = 400, .font_style = "normal", .font_size = 18.0f,
                .color = {0.42f, 0.44f, 0.52f, 1.0f},
                .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle,
                .line_height = 1.12f, .tracking = 0.1f, .max_lines = 9, .wrap = TextWrap::Word,
            });
        });

        return s.build();
    });
}

// ── ButterySmooth (custom dark magenta) ────────────────────────────────────────
Composition text_premium_hero_buttery_smooth() {
    auto cfg = style::dark_saas_neon();
    cfg.name = "ButterySmooth";
    cfg.palette.bg          = {0.03f, 0.01f, 0.05f, 1.0f};
    cfg.palette.bg_gradient = {0.02f, 0.01f, 0.04f, 1.0f};
    cfg.palette.accent      = {0.90f, 0.18f, 0.92f, 1.0f};
    cfg.palette.accent_glow = {0.90f, 0.18f, 0.92f, 1.0f};
    cfg.palette.cta_start   = {1.0f, 0.30f, 0.92f, 1.0f};
    cfg.palette.cta_end     = {0.94f, 0.18f, 0.98f, 1.0f};
    cfg.palette.text_main   = {1.0f, 0.30f, 0.92f, 1.0f};
    cfg.palette.text_sub    = {0.80f, 0.86f, 0.98f, 1.0f};
    cfg.palette.cta_glow    = {0.90f, 0.18f, 0.92f, 0.10f};
    cfg.fx.hero_glow_radius = 22.0f;
    cfg.fx.hero_glow_intensity = 0.55f;
    cfg.fx.use_background_shapes = false;

    using namespace style::component;

    return composition({.name = "TextPremiumHeroButterySmooth", .width = 1920, .height = 1080, .duration = 1}, [cfg](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Background
        s.layer("bg", [cfg](LayerBuilder& l) {
            l.fill(cfg.palette.bg);
        });

        // Ambient magenta blobs
        premium::add_ambient_blob(s, "ambient_left",  {-420.0f, -30.0f, 0.0f}, 320.0f, {0.86f,0.14f,0.82f,0.14f}, 210.0f, 0.72f);
        premium::add_ambient_blob(s, "ambient_right", {460.0f, 40.0f, 0.0f},  260.0f, {0.84f,0.14f,0.84f,0.12f}, 190.0f, 0.68f);
        premium::add_premium_grid(s, {1.0f, 1.0f, 1.0f, 0.035f}, 72.0f);
        premium::add_soft_orb(s, "violet_orb",  {-340.0f, 0.0f, 0.0f}, 260.0f, {0.92f,0.16f,0.88f,0.18f}, 140.0f);
        premium::add_soft_orb(s, "magenta_orb", {420.0f, 20.0f, 0.0f}, 220.0f, {0.88f,0.12f,0.86f,0.16f}, 120.0f);

        // Frame panel
        s.layer("frame", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("panel", {
                .size={1480.0f,640.0f}, .radius=40.0f, .color={0.06f,0.02f,0.10f,0.40f}, .pos={0,0,0},
                .fill=Fill::linear({0,0},{1,1},{{0,{0.10f,0.02f,0.16f,0.32f}},{1,{0.26f,0.04f,0.22f,0.34f}}}),
            });
            l.drop_shadow({0.0f, 26.0f}, {0.0f, 0.0f, 0.0f, 0.58f}, 52.0f);
            l.glow(22.0f, 0.55f, {0.90f, 0.18f, 0.92f, 1.0f});
        });

        // Buttery / Smooth / star as a single inline rich-text line.
        s.layer("hero", [cfg](LayerBuilder& l) {
            l.position({0.0f, -24.0f, 0.0f});
            l.drop_shadow({0.0f, 10.0f}, {0.0f, 0.0f, 0.0f, 0.26f}, 20.0f);
            l.glow(18.0f, 0.32f, {0.90f, 0.18f, 0.92f, 1.0f});

            RichTextLine line;
            line.run("Buttery", {1,1,1,1}, 120.0f, "assets/fonts/Inter-Bold.ttf")
                .size(120.0f)
                .tracking(-3.0f)
                .paint(TextPaint{
                    .fill = {1,1,1,1},
                    .fill_style = Fill::linear({0,0},{1,0},{
                        {0,{1,0.30f,0.92f,1}},
                        {0.55f,{0.98f,0.12f,0.86f,1}},
                        {1,{0.94f,0.18f,0.98f,1}}
                    }),
                    .stroke_enabled = true,
                    .stroke_color = {0.12f,0.04f,0.18f,1},
                    .stroke_width = 1.2f,
                });
            line.space(24.0f);
            line.run("Smooth", {1,1,1,1}, 120.0f, "assets/fonts/Inter-Bold.ttf")
                .size(120.0f)
                .tracking(-2.0f)
                .paint(TextPaint{
                    .fill = {1,1,1,1},
                    .fill_style = Fill::linear({0,0},{1,0},{
                        {0,{1,1,1,1}},
                        {0.55f,{0.95f,0.98f,1,1}},
                        {1,{0.80f,0.86f,0.98f,1}}
                    }),
                    .stroke_enabled = true,
                    .stroke_color = {0.88f,0.16f,0.82f,0},
                    .stroke_width = 0.0f,
                });
            line.space(28.0f);
            line.star({1,0.20f,0.92f,1}, 42.0f, 12.0f, 8);

            draw_rich_text(
                l,
                line,
                {0.0f, 0.0f, 0.0f},
                {
                    .anchor = RichTextAnchor::Center,
                    .vertical_anchor = RichTextVerticalAnchor::Middle,
                    .glyph_padding = 4.0f,
                    .snap_to_pixels = true,
                }
            );
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("TextPremiumHero",               chronon3d::content::text::text_premium_hero)
CHRONON_REGISTER_COMPOSITION("TextPremiumHeroSaaSBlue",      chronon3d::content::text::text_premium_hero_saas_blue)
CHRONON_REGISTER_COMPOSITION("TextPremiumHeroExplainer",     chronon3d::content::text::text_premium_hero_explainer)
CHRONON_REGISTER_COMPOSITION("TextPremiumHeroButterySmooth",  chronon3d::content::text::text_premium_hero_buttery_smooth)
} // namespace chronon3d::content::text
