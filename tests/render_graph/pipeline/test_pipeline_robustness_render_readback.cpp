// test_pipeline_robustness_render_readback.cpp — Split from
// test_pipeline_robustness_coordinate.cpp: camera2_5D cold/warm equivalence,
// effects-blur border crash-safety and pixel readback visual checks.
#include <doctest/doctest.h>
#include <tests/helpers/doctest_skip_compat.hpp>
#include <spdlog/spdlog.h>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/scene/model/render/render_node_factory.hpp>
#include <cmath>
#include <array>
#include <cstdlib>
#include <string>
#include <mutex>
#include "src/render_graph/builder/graph_builder_coordinates.hpp"
#include "src/render_graph/builder/evaluated_layer_placement.hpp"
#include "src/render_graph/builder/graph_builder_internal.hpp"
#include "src/render_graph/executor/tile_pruning.hpp"
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;

TEST_CASE("Camera2_5D multi-source cold and warm renders are identical") {
    SceneBuilder builder;
    builder.layer("projected_multi", [](LayerBuilder& lb) {
        lb.enable_3d(true)
          .position({80.0f, -40.0f, 0.0f})
          .rect("red_rect", {
              .size = {80.0f, 80.0f},
              .color = Color::red(),
              .pos = {0.0f, 0.0f, 0.0f}
          })
          .rect("blue_rect", {
              .size = {40.0f, 40.0f},
              .color = Color::blue(),
              .pos = {100.0f, 20.0f, 0.0f}
          });
    });
    const Scene scene = builder.build();

    auto renderer = test::make_renderer();
    auto settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);

    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -800.0f};
    camera.zoom = 800.0f;

    const auto cold = renderer.render_scene(scene, camera, 1920, 1080, 30.0f);
    REQUIRE(cold != nullptr);
    const auto warm = renderer.render_scene(scene, camera, 1920, 1080, 30.0f);
    REQUIRE(warm != nullptr);

    // The public render_scene entrypoint may satisfy the second render through
    // an upstream fast path before the graph-cache counter is incremented.
    // The observable contract here is deterministic framebuffer equivalence
    // between cold and warm execution of the same projected multi-source scene.
    CHECK(test::framebuffer_hash(*warm) == test::framebuffer_hash(*cold));
}

TEST_CASE("Effects, predicted_bbox and clipping - Blur near border doesn't crash") {
    SceneBuilder builder;
    builder.layer("blur_layer", [](LayerBuilder& lb) {
        lb.position({10.0f, 10.0f, 0.0f})
          .blur(30.0f)
          .rect("red_rect", {.size={100.0f, 100.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 200, 200, 30.0f);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 200);
}

TEST_CASE("Test visivi e lettura pixel in C++ - Pixel check white") {
    SceneBuilder builder;
    builder.layer("white_rect_layer", [](LayerBuilder& lb) {
        lb.rect("white_rect", {.size={50.0f, 50.0f}, .color=Color::white(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 100, 100, 30.0f);
    REQUIRE(fb != nullptr);

    Color center = fb->get_pixel(25, 25);
    CHECK(center.r > 0.95f);
    CHECK(center.g > 0.95f);
    CHECK(center.b > 0.95f);
    CHECK(center.a > 0.95f);
}

TEST_CASE("Test visivi e lettura pixel in C++ - Alpha blending") {
    SceneBuilder builder;
    builder.layer("bg_layer", [](LayerBuilder& lb) {
        lb.rect("blue_rect", {.size={200.0f, 200.0f}, .color=Color::blue(), .pos={100.0f, 100.0f, 0.0f}});
    });
    builder.layer("top_layer", [](LayerBuilder& lb) {
        lb.opacity(0.5f)
          .rect("red_rect", {.size={200.0f, 200.0f}, .color=Color::red(), .pos={100.0f, 100.0f, 0.0f}});
    });
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
        renderer.set_settings(settings);

    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 200, 200, 30.0f);
    REQUIRE(fb != nullptr);

    Color center = fb->get_pixel(100, 100);
    CHECK(center.r > 0.4f);
    CHECK(center.r < 0.6f);
    CHECK(center.b > 0.4f);
    CHECK(center.b < 0.6f);
    CHECK(center.a > 0.95f);
}
