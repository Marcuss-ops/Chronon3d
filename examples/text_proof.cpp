#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/scene_builder.hpp>

namespace chronon3d {

Composition TextProof() {
    return composition({
        .name = "TextProof",
        .width = 1280,
        .height = 720,
        .frame_rate = {30, 1},
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);

        // Background
        s.rect("bg", {640, 360, 0}, Color{0.02f, 0.02f, 0.04f, 1}, {1280, 720});

        // Card
        s.rounded_rect(
            "card",
            {640, 360, 0},
            {760, 260},
            32,
            Color{0.10f, 0.12f, 0.20f, 1}
        ).with_shadow({
            .enabled = true,
            .offset = {0, 10},
            .color = {0, 0, 0, 0.5f},
            .radius = 20
        });

        // Title
        s.text(
            "title",
            "CHRONON3D",
            {330, 270, 0},
            TextStyle{
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .size = 72.0f,
                .color = Color{1, 1, 1, 1}
            }
        );

        // Subtitle
        s.text(
            "subtitle",
            "Code-first motion graphics",
            {335, 360, 0},
            TextStyle{
                .font_path = "assets/fonts/Inter-Regular.ttf",
                .size = 34.0f,
                .color = Color{0.75f, 0.80f, 1.0f, 1}
            }
        );

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("TextProof", TextProof)

} // namespace chronon3d
