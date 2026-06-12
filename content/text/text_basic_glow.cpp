// content/text/text_basic_glow.cpp
//
// Basic composition: grid background + centered text + soft glow.
// Minimal, clean — one layer, one message.
// Uses simple GlowParams (not TextGlowSpec V2) to avoid rectangular
// bloom artifacts from the downscaled outer layer.
//
// Render:
//   chronon3d_cli render TextBasicGlow --frame 60 -o output/TextBasicGlow.png

#include "content/text/text_theme.hpp"

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::text {

// ═════════════════════════════════════════════════════════════════════════════
//  TextBasicGlow — grid background + centered text + soft white glow
//  One layer, clean, essential. Uses single-pass glow (no MultiLayer) for
//  a smooth, artifact-free radial falloff.
// ═════════════════════════════════════════════════════════════════════════════

Composition text_basic_glow() {
    return composition({.name = "TextBasicGlow", .width = 1920, .height = 1080, .duration = 60},
    [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        add_text_background(s);

        // Clean single-pass glow — soft white, no rectangular bloom
        const GlowParams glow = {
            .enabled = true,
            .radius = 20.0f,
            .intensity = 0.50f,
            .color = {0.80f, 0.85f, 1.0f, 1.0f},  // soft white-blue
            .threshold = 0.0f,
            .spread = 1.0f,
            .softness = 1.0f,
            .falloff = 0.85f,
            .preserve_source = true,
            .additive = false,
            .quality = GlowQuality::Standard,
        };

        const f32 op = fade_in(ctx.frame, 0.0f, 20.0f);

        s.layer("hero", [op, glow](LayerBuilder& l) {
            l.pin_to(Anchor::Center)
             .position({0.0f, 0.0f, 0.0f})
             .opacity(op)
             .glow(glow);
            l.text("title", {
                .text = "CHRONON3D",
                .size = {1600.0f, 160.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = TEXT_FONT_PATH,
                .font_size = 96.0f,
                .color = FRESH_TEXT_WHITE,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 16.0f,
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text
