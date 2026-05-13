#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/scene_builder.hpp>
#include <vector>

using namespace chronon3d;

static Composition DebugProjection() {
    return composition({
        .name = "DebugProjection",
        .width = 1280,
        .height = 720,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true,
            .position = {0, 0, -1000},
            .projection_mode = Camera2_5DProjectionMode::Fov,
            .fov_deg = 45.0f
        });

        s.layer("bg", [](LayerBuilder& l) {
            l.rect("bg", RectParams{.size={2000, 2000}, .color=Color{0.1f, 0.1f, 0.12f, 1.0f}});
        });

        s.layer("card", [&](LayerBuilder& l) {
            l.position({0, 0, 0})
             .rotate({0, 85, 0}) // Extreme rotation
             .enable_3d(true)
             .rect("rect", RectParams{.size={400, 400}, .color=Color::white()});
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("DebugProjection", DebugProjection)
