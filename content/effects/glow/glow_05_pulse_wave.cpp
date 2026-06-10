// content/effects/glow_05_pulse_wave.cpp
// TEST 5 — Pulse Wave: animated concentric rings + text pulsating in sync
#include "../common/glow_test_common.hpp"

namespace chronon3d::content::effects {

Composition glow_05_pulse_wave() {
    return composition({.name="GlowPulseWave",.width=kW,.height=kH,.duration=120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.01f,0.02f,0.04f,1}, Color{0.02f,0.04f,0.08f,1});

        const float t  = static_cast<float>(ctx.frame.frame.frame) / 120.f;
        const float p  = 0.5f + 0.5f * std::sinf(t * 6.2831853f * 2.f);
        const float p2 = 0.5f + 0.5f * std::sinf(t * 6.2831853f * 2.f + 1.0f);
        const float p3 = 0.5f + 0.5f * std::sinf(t * 6.2831853f * 2.f + 2.1f);

        // Outer ring — large soft cyan glow
        s.layer("ring_outer", [p](LayerBuilder& l) {
            l.position({0, 0, 0})
             .glow(140.f + p*40.f, 0.40f + p*0.45f, Color{0.18f,0.78f,1.f,1.f});
            // Invisible circle just to anchor the glow
            l.circle("c", {.radius=190.f,.color=Color{0,0,0,0},.pos={0,0,0}});
        });

        // Mid ring — purple
        s.layer("ring_mid", [p2](LayerBuilder& l) {
            l.position({0, 0, 0})
             .glow(80.f + p2*30.f, 0.70f + p2*0.50f, Color{0.65f,0.25f,1.f,1.f});
            l.circle("c", {.radius=110.f,.color=Color{0,0,0,0},.pos={0,0,0}});
        });

        // Inner ring — magenta
        s.layer("ring_inner", [p3](LayerBuilder& l) {
            l.position({0, 0, 0})
             .glow(40.f + p3*20.f, 1.0f + p3*0.6f, Color{1.f,0.22f,0.72f,1.f});
            l.circle("c", {.radius=60.f,.color=Color{0,0,0,0},.pos={0,0,0}});
        });

        // Core bright orb
        s.layer("core", [p](LayerBuilder& l) {
            l.position({0, 0, 0})
             .glow(30.f, 1.8f + p*0.5f, Color{0.90f,0.97f,1.f,1.f});
            l.circle("c", {.radius=32.f + p*8.f,.color=Color{1,1,1,0.95f},.pos={0,0,0}});
        });

        // Pulsing label
        s.layer("pulse_text", [p](LayerBuilder& l) {
            l.position({0, 340, 0})
             .glow(18.f, 0.7f + p*0.6f, Color{0.18f,0.78f,1.f,1.f});
            l.text("t", {
                .text="PULSE WAVE",
                .size={700,60},.pos={0,0,0},
                .font_size=38.f,
                .color=Color{0.70f,0.92f,1.f,1.f},
                .align=TextAlign::Center,.vertical_align=VerticalAlign::Middle
            });
        });

        bottom_label(s, "TEST 5 — Pulse Wave: animated concentric rings glow in sync");
        return s.build();
    });
}

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("GlowPulseWave", chronon3d::content::effects::glow_05_pulse_wave)
