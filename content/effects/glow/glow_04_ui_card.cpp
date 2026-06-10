// content/effects/glow_04_ui_card.cpp
// TEST 4 — UI Card: glassmorphism card with glowing border + icon + ambient orbs
#include "../common/glow_test_common.hpp"

namespace chronon3d::content::effects {

Composition glow_04_ui_card() {
    return composition({.name="GlowUICard",.width=kW,.height=kH,.duration=90},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.04f,0.04f,0.10f,1}, Color{0.02f,0.02f,0.06f,1});

        // Ambient background orbs (large soft glow)
        s.layer("amb_blue", [](LayerBuilder& l) {
            l.position({-420,-180,0})
             .glow(GlowParams{.radius = 220.f, .intensity = 0.50f, .color = Color{0.18f,0.42f,1.f,1.f}});
            l.circle("c", {.radius=90.f,.color=Color{0.1f,0.2f,0.6f,0.25f},.pos={0,0,0}});
        });
        s.layer("amb_purple", [](LayerBuilder& l) {
            l.position({420,200,0})
             .glow(GlowParams{.radius = 180.f, .intensity = 0.42f, .color = Color{0.78f,0.20f,1.f,1.f}});
            l.circle("c", {.radius=70.f,.color=Color{0.4f,0.1f,0.5f,0.25f},.pos={0,0,0}});
        });

        // Card dark background (glass)
        s.layer("card_bg", [](LayerBuilder& l) {
            l.rounded_rect("bg", {
                .size={720,440},.radius=28.f,
                .color=Color{0.07f,0.08f,0.13f,0.96f},
                .pos={-360,-220,0}
            });
        });

        // Card glow border (cyan, thin outline)
        s.layer("card_border", [](LayerBuilder& l) {
            l.position({0,0,0})
             .glow(GlowParams{.radius = 26.f, .intensity = 1.1f, .color = Color{0.22f,0.62f,1.f,1.f}});
            const float hw=360.f,hh=220.f,r=28.f,k=r*0.552f;
            l.path("border", {
                .commands={
                    PathCommand::move_to({-hw+r,-hh}),
                    PathCommand::line_to({hw-r,-hh}),
                    PathCommand::cubic_to({hw-r+k,-hh},{hw,-hh+r-k},{hw,-hh+r}),
                    PathCommand::line_to({hw,hh-r}),
                    PathCommand::cubic_to({hw,hh-r+k},{hw-r+k,hh},{hw-r,hh}),
                    PathCommand::line_to({-hw+r,hh}),
                    PathCommand::cubic_to({-hw+r-k,hh},{-hw,hh-r+k},{-hw,hh-r}),
                    PathCommand::line_to({-hw,-hh+r}),
                    PathCommand::cubic_to({-hw,-hh+r-k},{-hw+r-k,-hh},{-hw+r,-hh}),
                    PathCommand::close(),
                },
                .stroke={.width=1.5f,.cap=LineCap::Round,.join=LineJoin::Round},
                .fill=Fill::solid_color(Color{0,0,0,0}),
                .color=Color{0.30f,0.62f,1.f,0.75f},
                .closed=true
            });
        });

        // Card icon — glowing cyan circle
        s.layer("icon", [](LayerBuilder& l) {
            l.position({0,-80,0})
             .glow(GlowParams{.radius = 32.f, .intensity = 1.3f, .color = Color{0.22f,0.82f,1.f,1.f}});
            l.circle("c", {.radius=44.f,.color=Color{0.22f,0.72f,1.f,0.88f},.pos={0,0,0}});
        });

        // Card title
        s.layer("card_title", [](LayerBuilder& l) {
            l.position({0,70,0});
            l.text("t", {
                .text="GLOW UI CARD",
                .size={640,70},.pos={0,0,0},
                .font_size=36.f,.color=Color{1,1,1,1},
                .align=TextAlign::Center,.vertical_align=VerticalAlign::Middle
            });
        });

        // Card subtitle
        s.layer("card_sub", [](LayerBuilder& l) {
            l.position({0,140,0});
            l.text("t", {
                .text="Glassmorphism with luminous glow border",
                .size={640,44},.pos={0,0,0},
                .font_size=18.f,.color=Color{0.58f,0.72f,0.88f,1},
                .align=TextAlign::Center,.vertical_align=VerticalAlign::Middle
            });
        });

        bottom_label(s, "TEST 4 — UI Card: glass card with glowing cyan border + ambient orbs");
        return s.build();
    });
}

} // namespace chronon3d::content::effects

