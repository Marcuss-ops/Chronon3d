#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

TEST_CASE("Camera smoke test renders a centered 3D layer") {
    Composition comp({
        .name = "CameraSmokeTest",
        .width = 128,
        .height = 128,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled = true,
            .position = {0.0f, 0.0f, -1000.0f},
            .zoom = 1000.0f
        });

        s.layer("card", [](LayerBuilder& l) {
            l.enable_3d().rect("box", {
                .size = {48.0f, 48.0f},
                .color = Color::white(),
                .pos = {64.0f, 64.0f, 0.0f},
            });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 128);
    CHECK(fb->height() == 128);

    const Color center = fb->get_pixel(64, 64);
    CHECK(center.a > 0.5f);
    CHECK(center.r > 0.5f);
    CHECK(center.g > 0.5f);
    CHECK(center.b > 0.5f);
}
