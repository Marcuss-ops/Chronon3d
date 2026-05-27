// content/effects/glow_01_neon_sign.cpp
// TEST 1 — Neon Sign: electric blue + magenta hero text on pure black
#include "glow_test_common.hpp"

namespace chronon3d::content::effects {

Composition glow_01_neon_sign() {
    return composition({.name="GlowNeonSign",.width=kW,.height=kH,.duration=120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.01f,0.01f,0.03f,1}, Color{0.04f,0.02f,0.07f,1});

        // Subtle horizontal scan lines
        s.layer("scanlines", [](LayerBuilder& l) {
            for (int i = 0; i < 10; ++i) {
                float y = -kHH + 110.f * i;
                l.line("h"+std::to_string(i), {
                    .from={-kHW,y,0}, .to={kHW,y,0},
                    .thickness=1.f, .color=Color{1,1,1,0.03f}
                });
            }
        });

        // Top decorative bar — electric blue
        s.layer("bar_top", [](LayerBuilder& l) {
            l.position({0, -kHH + 140.f, 0})
             .glow(16.f, 1.4f, Color{0.18f,0.62f,1.f,1.f});
            l.rect("r", {.size={800,2},.color=Color{0.5f,0.85f,1.f,1.f},.pos={-400,-1,0}});
        });

        // Hero text — massive, electric blue glow
        s.layer("hero", [](LayerBuilder& l) {
            l.position({0, -60, 0})
             .glow(55.f, 1.7f, Color{0.18f,0.62f,1.f,1.f});
            l.text("t", {
                .text="NEON\nGLOW",
                .size={1100,400}, .pos={0,0,0},
                .font_size=150.f,
                .color=Color{0.88f,0.96f,1.f,1.f},
                .align=TextAlign::Center,
                .vertical_align=VerticalAlign::Middle,
                .line_height=0.88f
            });
        });

        // Tagline — magenta glow
        s.layer("tagline", [](LayerBuilder& l) {
            l.position({0, 260, 0})
             .glow(22.f, 1.2f, Color{1.f,0.20f,0.72f,1.f});
            l.text("t", {
                .text="CHRONON3D · GLOW PIPELINE · TEST 1",
                .size={1100,50}, .pos={0,0,0},
                .font_size=24.f,
                .color=Color{1.f,0.62f,0.92f,1.f},
                .align=TextAlign::Center,
                .vertical_align=VerticalAlign::Middle
            });
        });

        // Bottom bar — magenta
        s.layer("bar_bot", [](LayerBuilder& l) {
            l.position({0, kHH - 140.f, 0})
             .glow(16.f, 1.2f, Color{1.f,0.20f,0.72f,1.f});
            l.rect("r", {.size={800,2},.color=Color{1.f,0.5f,0.9f,1.f},.pos={-400,-1,0}});
        });

        bottom_label(s, "TEST 1 — Neon Sign: electric blue + magenta glow on pure black");
        return s.build();
    });
}

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("GlowNeonSign", chronon3d::content::effects::glow_01_neon_sign)
