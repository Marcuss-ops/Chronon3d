#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/framebuffer.hpp>

using namespace chronon3d;

TEST_CASE("Renderer: Path Stroke") {
    SoftwareRenderer renderer;
    auto scene = SceneBuilder{}
        .path("stroke", {
            .commands = {
                PathCommand::move_to({10, 10}),
                PathCommand::line_to({90, 10}),
                PathCommand::line_to({90, 90}),
                PathCommand::line_to({10, 90}),
                PathCommand::close()
            },
            .stroke = {.width = 4.0f},
            .pos = {0, 0, 0}
        })
        .build();

    Camera camera; // Identity
    auto fb = renderer.render_scene(scene, camera, 100, 100);

    REQUIRE(fb != nullptr);
    // Check some pixels on the path
    // (10, 10) to (90, 10)
    CHECK(fb->get_pixel(50, 10).a > 0.5f);
    // (90, 10) to (90, 90)
    CHECK(fb->get_pixel(90, 50).a > 0.5f);
    // Center should be empty (since it's only stroke)
    CHECK(fb->get_pixel(50, 50).a == 0.0f);
}

TEST_CASE("Renderer: Path Trim") {
    SoftwareRenderer renderer;
    
    SUBCASE("Trim Half") {
        auto scene = SceneBuilder{}
            .path("trimmed", {
                .commands = {
                    PathCommand::move_to({0, 50}),
                    PathCommand::line_to({100, 50})
                },
                .stroke = {.width = 2.0f, .trim_end = 0.5f}
            })
            .build();

        auto fb = renderer.render_scene(scene, Camera{}, 100, 100);
        CHECK(fb->get_pixel(25, 50).a > 0.5f);
        CHECK(fb->get_pixel(75, 50).a == 0.0f);
    }
}
