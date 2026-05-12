#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/renderer/software_renderer.hpp>

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
        builder.rect("box", {50, 50, 0}, Color::white(), {50, 50, 1});
        return builder.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);

    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 100);
    CHECK(fb->height() == 100);

    // Verify that some pixels are rendered (not all black)
    bool has_content = false;
    for (i32 y = 0; y < fb->height(); ++y) {
        for (i32 x = 0; x < fb->width(); ++x) {
            Color c = fb->get_pixel(x, y);
            if (c.r > 0.0f || c.g > 0.0f || c.b > 0.0f) {
                has_content = true;
                break;
            }
        }
        if (has_content) break;
    }
    CHECK(has_content);

    // Specific check for center pixel (where the box should be)
    Color center = fb->get_pixel(50, 50);
    CHECK((center.r > 0.0f || center.g > 0.0f || center.b > 0.0f));
}
