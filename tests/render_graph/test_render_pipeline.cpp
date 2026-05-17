#include <doctest/doctest.h>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;

TEST_CASE("RenderPipeline - render_scene_via_graph produces valid framebuffer") {
    // Scenario: Scene with a red rectangle
    SceneBuilder builder;
    builder.rect("red_rect", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene = builder.build();

    SoftwareRenderer renderer;
    cache::NodeCache node_cache;
    Camera camera;

    auto fb = render_scene_via_graph(
        renderer,
        node_cache,
        scene,
        camera,
        100, 100,
        0, 0.0f,
        renderer.render_settings(),
        renderer.composition_registry(),
        renderer.video_decoder()
    );

    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 100);
    CHECK(fb->height() == 100);
    
    // The red rectangle should be visible and not fully transparent
    bool found_red = false;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            Color pixel = fb->get_pixel(x, y);
            if (pixel.r > 0.8f && pixel.g < 0.2f && pixel.b < 0.2f) {
                found_red = true;
                break;
            }
        }
    }
    CHECK(found_red);
}

TEST_CASE("RenderPipeline - SoftwareRenderer::render_scene produces same output as render_scene_via_graph") {
    SceneBuilder builder;
    builder.rect("blue_rect", {.size={60.0f, 60.0f}, .color=Color::blue(), .pos={10.0f, 10.0f, 0.0f}});
    Scene scene = builder.build();

    SoftwareRenderer renderer;
    Camera camera;

    auto fb_backend = renderer.render_scene(scene, camera, 100, 100);
    
    cache::NodeCache node_cache;
    auto fb_pipeline = render_scene_via_graph(
        renderer,
        node_cache,
        scene,
        camera,
        100, 100,
        0, 0.0f,
        renderer.render_settings(),
        renderer.composition_registry(),
        renderer.video_decoder()
    );

    REQUIRE(fb_backend != nullptr);
    REQUIRE(fb_pipeline != nullptr);
    CHECK(fb_backend->width() == fb_pipeline->width());
    CHECK(fb_backend->height() == fb_pipeline->height());

    // Compare all pixel values
    bool matches = true;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            Color c1 = fb_backend->get_pixel(x, y);
            Color c2 = fb_pipeline->get_pixel(x, y);
            if (std::abs(c1.r - c2.r) > 0.01f ||
                std::abs(c1.g - c2.g) > 0.01f ||
                std::abs(c1.b - c2.b) > 0.01f ||
                std::abs(c1.a - c2.a) > 0.01f) {
                matches = false;
                break;
            }
        }
    }
    CHECK(matches);
}

TEST_CASE("RenderPipeline - debug_render_graph passes through the unique pipeline and generates DOT representation") {
    SceneBuilder builder;
    builder.rect("rect", {.size={40.0f, 40.0f}, .color=Color::green(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene = builder.build();

    SoftwareRenderer renderer;
    Camera camera;

    std::string dot = renderer.debug_render_graph(scene, camera, 100, 100);

    // DOT output should contain standard clear/composite nodes
    CHECK(dot.find("Clear") != std::string::npos);
    CHECK(dot.find("rect") != std::string::npos);
    CHECK(dot.find("Composite") != std::string::npos);
}
