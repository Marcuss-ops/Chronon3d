#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/math/color.hpp>

#include <cmath>

#include "text_helpers.hpp"

namespace chronon3d::content::text {

Composition text_glow() {
    return composition({
        .name = "TextGlow",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 pulse = 0.6f + 0.4f * std::sin(p * 3.14159f * 4.0f);
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.005f, 0.005f, 0.02f, 1.0f});
        });

        s.layer("glow_text", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center);
            l.glow(40.0f * pulse, pulse, Color{0.25f, 0.52f, 1.0f});
            apply_text(l, "text", {
                .text = "GLOW EFFECT",
                .size = 110.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .tracking = 8.0f,
            }, {W * 0.8f, 160.0f});
        });

        return s.build();
    });
}

Composition text_shadow() {
    return composition({
        .name = "TextShadow",
        .width = 1920, .height = 1080,
        .duration = 60,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 op = std::min(1.0f, p * 3.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.05f, 0.05f, 0.06f, 1.0f});
        });

        s.layer("shadow_text", [&](LayerBuilder& l) {
            l.opacity(op).pin_to(Anchor::Center);
            l.drop_shadow({6.0f, 10.0f}, {0, 0, 0, 0.55f}, 16.0f);
            apply_text(l, "text", {
                .text = "DROP SHADOW",
                .size = 100.0f,
                .color = {1, 1, 1, 1},
                .align = TextAlign::Center,
                .tracking = 5.0f,
            }, {W * 0.8f, 140.0f});
        });

        return s.build();
    });
}

Composition text_pulse() {
    return composition({
        .name = "TextPulse",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 pulse = 0.5f + 0.5f * std::sin(p * 6.2832f * 2.0f);
        f32 scale = 1.0f + 0.05f * std::sin(p * 6.2832f * 2.0f);
        f32 op = std::min(1.0f, p * 4.0f);

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.008f, 0.010f, 0.025f, 1.0f});
        });

        s.layer("pulse_text", [&](LayerBuilder& l) {
            l.opacity(op * pulse)
             .pin_to(Anchor::Center)
             .scale({scale, scale, 1});
            apply_text(l, "text", {
                .text = "PULSING",
                .size = 100.0f,
                .color = {0.25f, 0.52f, 1.0f, 1},
                .align = TextAlign::Center,
                .tracking = 10.0f,
            }, {W * 0.8f, 140.0f});
        });

        s.layer("sub_pulse", [&](LayerBuilder& l) {
            l.opacity(op * (1.0f - pulse) * 0.6f)
             .pin_to(Anchor::Center);
            apply_text(l, "sub", {
                .text = "Pulse animation via opacity & scale",
                .size = 28.0f,
                .color = {0.8f, 0.8f, 0.85f, 1},
                .align = TextAlign::Center,
                .tracking = 2.0f,
            }, {W * 0.6f, 50.0f}, {0, 100, 0});
        });

        return s.build();
    });
}

} // namespace chronon3d::content::text
