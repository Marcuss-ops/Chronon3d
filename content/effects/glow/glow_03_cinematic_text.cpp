// content/effects/glow_03_cinematic_text.cpp
// TEST 3 — Cinematic Text: golden hero phrase + cyan subtitle on dark gradient
//
// Reference implementation of the new TextGlowSpec V2 API (AE-like multi-layer
// glow).  The legacy `l.glow(radius, intensity, color)` 3-param API is no
// longer used here — instead we build a TextGlowSpec and call
// l.glow(spec.to_glow_params()) for the multi-pass glow + a micro drop shadow
// underneath the text to keep it readable on dark backgrounds.
#include "../common/glow_test_common.hpp"
#include <chronon3d/text/text_glow_spec.hpp>

namespace chronon3d::content::effects {

Composition glow_03_cinematic_text() {
    return composition({.name="GlowCinematicText",.width=kW,.height=kH,.duration=120},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        deep_bg(s, Color{0.03f,0.02f,0.05f,1}, Color{0.06f,0.04f,0.02f,1});

        // ── Top golden rule (small accent line) ──────────────────────────
        s.layer("rule_top", [](LayerBuilder& l) {
            l.position({0, -310, 0});
            // Build a small rule glow: tight only, no bloom
            TextGlowSpec rule_glow;
            rule_glow.inner_radius    = 3.0f;
            rule_glow.mid_radius      = 0.0f;   // skip mid
            rule_glow.bloom_radius    = 0.0f;   // skip bloom
            rule_glow.inner_intensity = 0.9f;
            rule_glow.mid_intensity   = 0.0f;
            rule_glow.bloom_intensity = 0.0f;
            rule_glow.inner_color     = Color{1.f, 0.78f, 0.18f, 1.f};
            l.glow(rule_glow.to_glow_params());
            l.rect("r", {.size={700,2},.color=Color{1.f,0.85f,0.35f,0.9f},.pos={-350,-1,0}});
        });

        // ── Hero text — warm gold cinematic glow + micro shadow ──────────
        s.layer("hero", [](LayerBuilder& l) {
            l.position({0, -60, 0});
            // New V2 API: TextGlowSpec → multi-layer glow
            auto glow = TextGlowPresets::ae_cinematic_warm();
            glow.inner_radius    = 6.0f;   // a bit wider for huge 130pt text
            glow.mid_radius      = 22.0f;
            glow.bloom_radius    = 50.0f;
            glow.inner_intensity = 0.65f;
            glow.mid_intensity   = 0.28f;
            glow.bloom_intensity = 0.10f;
            l.glow(glow.to_glow_params());
            // Micro drop shadow (text-readable on dark background)
            l.drop_shadow({0.0f, 8.0f}, glow.micro_shadow_color, glow.micro_shadow_radius);
            l.text("t", {
                .text="THIS CHANGES\nEVERYTHING",
                .size={1200,380}, .pos={0,0,0},
                .font_path="assets/fonts/Poppins-Bold.ttf",
                .font_size=130.f,
                .color=Color{1.f,0.94f,0.80f,1.f},
                .align=TextAlign::Center,
                .vertical_align=VerticalAlign::Middle,
                .line_height=0.90f
            });
        });

        // ── Cyan subtitle with cool blue glow + micro shadow ─────────────
        s.layer("tagline", [](LayerBuilder& l) {
            l.position({0, 240, 0});
            auto glow = TextGlowPresets::ae_cinematic_white();
            // Cool blue tint for the cyan subtitle
            glow.inner_color = Color{0.62f, 0.94f, 1.0f, 1.0f};
            glow.outer_color = Color{0.22f, 0.70f, 1.0f, 1.0f};
            glow.inner_radius    = 4.0f;
            glow.mid_radius      = 14.0f;
            glow.bloom_radius    = 30.0f;
            glow.inner_intensity = 0.55f;
            glow.mid_intensity   = 0.24f;
            glow.bloom_intensity = 0.09f;
            l.glow(glow.to_glow_params());
            l.drop_shadow({0.0f, 4.0f}, glow.micro_shadow_color, glow.micro_shadow_radius);
            l.text("t", {
                .text="Glow keeps text sharp and luminous",
                .size={1000,60}, .pos={0,0,0},
                .font_path="assets/fonts/Poppins-Bold.ttf",
                .font_size=28.f,
                .color=Color{0.62f,0.94f,1.f,1.f},
                .align=TextAlign::Center,
                .vertical_align=VerticalAlign::Middle
            });
        });

        // ── Bottom golden rule (mirrors top) ─────────────────────────────
        s.layer("rule_bot", [](LayerBuilder& l) {
            l.position({0, 310, 0});
            TextGlowSpec rule_glow;
            rule_glow.inner_radius    = 3.0f;
            rule_glow.mid_radius      = 0.0f;
            rule_glow.bloom_radius    = 0.0f;
            rule_glow.inner_intensity = 0.9f;
            rule_glow.mid_intensity   = 0.0f;
            rule_glow.bloom_intensity = 0.0f;
            rule_glow.inner_color     = Color{1.f, 0.78f, 0.18f, 1.f};
            l.glow(rule_glow.to_glow_params());
            l.rect("r", {.size={700,2},.color=Color{1.f,0.85f,0.35f,0.9f},.pos={-350,-1,0}});
        });

        bottom_label(s, "TEST 3 — Cinematic Text: TextGlowSpec V2 (warm gold hero + cool blue subtitle)");
        return s.build();
    });
}

} // namespace chronon3d::content::effects

