#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

using namespace chronon3d;

TEST_CASE("Code-first rendering smoke test") {
    CompositionSpec spec{
        .name = "SmokeTest",
        .width = 100,
        .height = 100,
        .duration = 60
    };

    Composition comp(spec, [](const FrameContext& ctx) {
        SceneBuilder builder(ctx.resource);
        builder.rect("box", {.size={50, 50}, .color=Color::white(), .pos={50, 50, 0}});
        return builder.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 100);
    CHECK(fb->height() == 100);

    // Box is white at (50,50) size (50,50): center pixel must be non-black.
    const Color center = fb->get_pixel(50, 50);
    CHECK((center.r > 0.0f || center.g > 0.0f || center.b > 0.0f));
}
