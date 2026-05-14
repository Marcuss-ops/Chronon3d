#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d {

Composition TransformProof() {
    return composition({
        .name = "TransformProof",
        .width = 1280,
        .height = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        
        s.rect("bg", {
            .size = {1280, 720},
            .color = Color{0.05f, 0.05f, 0.07f, 1},
            .pos = {640, 360, 0}
        });

        // 1. Rotating Rect with Shadow
        auto rot = interpolate(ctx.frame, 0, 120, 0.0f, 360.0f);
        s.rect("rotating-box", {
            .size = {200, 100},
            .color = Color::red()
        })
        .at({300, 200, 0})
        .rotate({0, 0, rot})
        .with_shadow({
            .enabled = true,
            .offset = {10, 10},
            .color = {0, 0, 0, 0.6f},
            .radius = 15
        });

        // 2. Scaling Circle with Glow
        auto sc = interpolate(ctx.frame, 0, 120, 0.5f, 1.5f, Easing::InOutQuad);
        s.circle("scaling-circle", {
            .radius = 50,
            .color = Color::green()
        })
        .at({640, 360, 0})
        .scale({sc, sc, 1.0f})
        .with_glow({
            .enabled = true,
            .radius = 20,
            .intensity = 0.8f,
            .color = Color::green()
        });

        // 3. Transparent Rounded Rect
        auto op = interpolate(ctx.frame, 0, 120, 1.0f, 0.1f);
        s.rounded_rect("fading-card", {
            .size = {300, 150},
            .radius = 24,
            .color = Color::blue()
        })
        .at({980, 520, 0})
        .opacity(op)
        .rotate({0, 0, -rot * 0.5f})
        .with_shadow({
            .enabled = true,
            .offset = {0, 15},
            .color = {0, 0, 0, 0.4f},
            .radius = 20
        });

        // 4. Line transform
        s.line("line", {
            .from = {0, 0, 0},
            .to = {100, 0, 0},
            .color = Color::yellow()
        })
        .at({640, 100, 0})
        .scale({sc * 2.0f, 1.0f, 1.0f})
        .rotate({0, 0, rot * 2.0f});

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("TransformProof", TransformProof)

} // namespace chronon3d
