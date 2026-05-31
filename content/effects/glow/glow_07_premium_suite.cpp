// content/effects/glow_07_premium_suite.cpp
// TEST 7 — Premium Glow Suite: side-by-side premium glow presets
#include "../common/glow_test_common.hpp"

namespace chronon3d::content::effects {

Composition glow_07_premium_suite() {
    return composition({.name="GlowPremiumSuite",.width=kW,.height=kH,.duration=120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.015f, 0.017f, 0.028f, 1.0f}, Color{0.030f, 0.025f, 0.045f, 1.0f});

        auto panel = [&](const std::string& key, Vec2 center, GlowParams glow, Color title_color,
                         const std::string& title, const std::string& subtitle) {
            s.layer(key + "_panel", [=](LayerBuilder& l) {
                l.position({center.x, center.y, 0.0f});
                l.rounded_rect("panel", {
                    .size = {580.0f, 260.0f},
                    .radius = 24.0f,
                    .color = Color{0.06f, 0.07f, 0.11f, 0.94f},
                    .pos = {-290.0f, -130.0f, 0.0f}
                });
                l.path("frame", {
                    .commands = {
                        PathCommand::move_to({-268.0f, -108.0f}),
                        PathCommand::line_to({268.0f, -108.0f}),
                        PathCommand::line_to({268.0f, 108.0f}),
                        PathCommand::line_to({-268.0f, 108.0f}),
                        PathCommand::close()
                    },
                    .stroke = {.width = 1.4f, .cap = LineCap::Round, .join = LineJoin::Round},
                    .fill = Fill::solid_color(Color{0, 0, 0, 0}),
                    .color = Color{0.32f, 0.36f, 0.48f, 0.70f},
                    .closed = true
                });
            });

            s.layer(key + "_accent", [=](LayerBuilder& l) {
                l.position({center.x, center.y - 78.0f, 0.0f})
                 .glow(glow);
                l.rect("accent", {
                    .size = {220.0f, 3.0f},
                    .color = Color{title_color.r, title_color.g, title_color.b, 0.95f},
                    .pos = {-110.0f, -1.5f, 0.0f}
                });
            });

            s.layer(key + "_title", [=](LayerBuilder& l) {
                l.position({center.x, center.y - 14.0f, 0.0f})
                 .glow(glow);
                l.text("title", {
                    .text = title,
                    .size = {500.0f, 68.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_size = 33.0f,
                    .color = title_color,
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle,
                    .line_height = 0.95f
                });
            });

            s.layer(key + "_sub", [=](LayerBuilder& l) {
                l.position({center.x, center.y + 62.0f, 0.0f})
                 .glow(glow);
                l.text("sub", {
                    .text = subtitle,
                    .size = {500.0f, 52.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .font_path = "assets/fonts/Inter-Regular.ttf",
                    .font_family = "Inter",
                    .font_size = 18.0f,
                    .color = Color{0.78f, 0.84f, 0.92f, 1.0f},
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle,
                    .line_height = 1.0f
                });
            });
        };

        panel("soft", {-350.0f, -175.0f},
              GlowPresets::soft_premium(34.0f),
              Color{0.72f, 0.90f, 1.0f, 1.0f},
              "SOFT PREMIUM",
              "screen blend, lower intensity, clean typography");

        panel("gold", {350.0f, -175.0f},
              GlowPresets::cinematic_gold_premium(52.0f),
              Color{1.0f, 0.86f, 0.45f, 1.0f},
              "CINEMATIC GOLD",
              "warm glow with a tighter core and softer bloom");

        panel("neon", {-350.0f, 185.0f},
              GlowPresets::neon_sign(40.0f),
              Color{0.28f, 0.78f, 1.0f, 1.0f},
              "NEON SIGN",
              "stronger additive glow for signage and punch");

        panel("editorial", {350.0f, 185.0f},
              GlowPresets::editorial_highlight(24.0f),
              Color{0.96f, 0.98f, 1.0f, 1.0f},
              "EDITORIAL",
              "very light highlight glow for premium UI and titles");

        bottom_label(s, "TEST 7 — Premium glow suite: soft premium, cinematic gold, neon sign, editorial highlight");
        return s.build();
    });
}

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("GlowPremiumSuite", chronon3d::content::effects::glow_07_premium_suite)
