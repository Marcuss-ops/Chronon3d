// content/effects/glow_03_cinematic_text.cpp
// TEST 3 — Cinematic Text: golden hero phrase + cyan subtitle on dark gradient
#include "glow_test_common.hpp"

namespace chronon3d::content::effects {

Composition glow_03_cinematic_text() {
    return composition({.name="GlowCinematicText",.width=kW,.height=kH,.duration=120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.03f,0.02f,0.05f,1}, Color{0.06f,0.04f,0.02f,1});

        // Top golden rule
        s.layer("rule_top", [](LayerBuilder& l) {
            l.position({0, -310, 0})
             .glow(10.f, 1.0f, Color{1.f,0.78f,0.18f,1.f});
            l.rect("r", {.size={700,2},.color=Color{1.f,0.85f,0.35f,0.9f},.pos={-350,-1,0}});
        });

        // Hero text — warm gold glow, huge
        s.layer("hero", [](LayerBuilder& l) {
            l.position({0, -60, 0})
             .glow(50.f, 1.5f, Color{1.f,0.78f,0.18f,1.f});
            l.text("t", {
                .text="THIS CHANGES\nEVERYTHING",
                .size={1200,380}, .pos={0,0,0},
                .font_size=130.f,
                .color=Color{1.f,0.94f,0.80f,1.f},
                .align=TextAlign::Center,
                .vertical_align=VerticalAlign::Middle,
                .line_height=0.90f
            });
        });

        // Cyan subtitle with glow
        s.layer("tagline", [](LayerBuilder& l) {
            l.position({0, 240, 0})
             .glow(24.f, 1.1f, Color{0.22f,0.88f,1.f,1.f});
            l.text("t", {
                .text="Glow keeps text sharp and luminous",
                .size={1000,60}, .pos={0,0,0},
                .font_size=28.f,
                .color=Color{0.62f,0.94f,1.f,1.f},
                .align=TextAlign::Center,
                .vertical_align=VerticalAlign::Middle
            });
        });

        // Bottom golden rule
        s.layer("rule_bot", [](LayerBuilder& l) {
            l.position({0, 310, 0})
             .glow(10.f, 1.0f, Color{1.f,0.78f,0.18f,1.f});
            l.rect("r", {.size={700,2},.color=Color{1.f,0.85f,0.35f,0.9f},.pos={-350,-1,0}});
        });

        bottom_label(s, "TEST 3 — Cinematic Text: gold hero phrase + cyan tagline glow");
        return s.build();
    });
}

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("GlowCinematicText", chronon3d::content::effects::glow_03_cinematic_text)
