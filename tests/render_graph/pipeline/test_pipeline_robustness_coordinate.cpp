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

TEST_CASE("Coordinate Centered vs Top Left - 2D standard centered layer") {
    SceneBuilder builder;
    builder.layer("2d_layer", [](LayerBuilder& lb) {
        lb.rect("red_rect", {.size={100.0f, 100.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.diagnostics.enabled = false;
    renderer.set_settings(settings);

    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 200, 200, 30.0f);
    REQUIRE(fb != nullptr);

    // With the modular (centered) coordinate system the 100x100 rect is
    // centered on the 200x200 canvas, so it covers (50,50)-(150,150).
    Color p_center = fb->get_pixel(100, 100);
    Color p_in = fb->get_pixel(50, 50);
    Color p_out = fb->get_pixel(160, 160);

    std::fprintf(stderr, "=== DEBUG pixels ===\n");
    std::fprintf(stderr, "p_center: r=%f, g=%f, b=%f, a=%f\n", p_center.r, p_center.g, p_center.b, p_center.a);
    std::fprintf(stderr, "p_in: r=%f, g=%f, b=%f, a=%f\n", p_in.r, p_in.g, p_in.b, p_in.a);
    std::fprintf(stderr, "p_out: r=%f, g=%f, b=%f, a=%f\n", p_out.r, p_out.g, p_out.b, p_out.a);
    std::fprintf(stderr, "====================\n");

    CHECK(p_center.r > 0.9f);
    CHECK(p_center.g < 0.1f);
    CHECK(p_center.b < 0.1f);
    CHECK(p_center.a > 0.9f);

    CHECK(p_in.r > 0.9f);
    CHECK(p_in.g < 0.1f);
    CHECK(p_in.b < 0.1f);
    CHECK(p_in.a > 0.9f);

    CHECK(p_out.a < 0.05f);
}

TEST_CASE("Coordinate Centered vs Top Left - Opacity only keeps implicit centering") {
    SceneBuilder builder;
    builder.layer("opacity_only", [](LayerBuilder& lb) {
        lb.opacity(0.5f);
        lb.rect("red_rect", {
            .size={1536.0f, 1024.0f},
            .color=Color::red(),
            .pos={0.0f, 0.0f, 0.0f}
        });
    });
    Scene scene = builder.build();

    RenderGraphContext ctx;
    ctx.frame_input.width = 1536;
    ctx.frame_input.height = 1024;
    ctx.frame_input.frame = 0;
    ctx.policy.modular_coordinates = true;

    auto resolved = chronon3d::graph::detail::resolve_layers(scene, ctx);
    REQUIRE(!resolved.layers.empty());

    const auto& rl = resolved.layers.front();
    LayerGraphItem item{
        .layer = rl.layer,
        .transform = rl.world_transform,
        .world_matrix = rl.world_matrix,
        .projected = false,
        .native_3d = false,
    };

    CHECK(chronon3d::graph::detail::is_implicit_2d_centering_only(item, ctx));
    CHECK(!chronon3d::graph::detail::has_custom_render_transform(item, ctx));
    CHECK(!chronon3d::graph::detail::layer_needs_render_transform(item, ctx));

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
        renderer.set_settings(settings);

    Camera camera;
    auto fb = renderer.render_scene(scene, camera, 1536, 1024, 30.0f);
    REQUIRE(fb != nullptr);

    CHECK(fb->get_pixel(10, 10).r > 0.4f);
    CHECK(fb->get_pixel(10, 10).a > 0.4f);
    CHECK(fb->get_pixel(1526, 10).r > 0.4f);
    CHECK(fb->get_pixel(10, 1014).r > 0.4f);
    CHECK(fb->get_pixel(1526, 1014).r > 0.4f);
    CHECK(fb->get_pixel(768, 512).r > 0.4f);
}

TEST_CASE("Coordinate Centered vs Top Left - Centered exactly on canvas") {
    SceneBuilder builder;
    builder.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    builder.layer("3d_layer", [](LayerBuilder& lb) {
        lb.enable_3d(true)
          .rect("red_rect", {.size={200.0f, 200.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.diagnostics.enabled = false;
    renderer.set_settings(settings);

    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -800.0f};
    camera.zoom = 800.0f;

    auto fb = renderer.render_scene(scene, camera, 1920, 1080, 30.0f);
    REQUIRE(fb != nullptr);

    Color p_in_0 = fb->get_pixel(860, 440);
    Color p_in_1 = fb->get_pixel(1059, 639);
    Color p_out_0 = fb->get_pixel(859, 440);
    Color p_out_1 = fb->get_pixel(860, 439);
    Color p_out_2 = fb->get_pixel(1060, 640);

    CHECK(p_in_0.r > 0.9f);
    CHECK(p_in_0.a > 0.9f);
    CHECK(p_in_1.r > 0.9f);
    CHECK(p_in_1.a > 0.9f);

    CHECK(p_out_0.a < 0.05f);
    CHECK(p_out_1.a < 0.05f);
    CHECK(p_out_2.a < 0.05f);
}

TEST_CASE("Coordinate Centered vs Top Left - Reversible conversion logic") {
    f32 w = 1920.0f;
    f32 h = 1080.0f;

    auto to_centered = [w, h](Vec2 tl) -> Vec2 {
        return { tl.x - w * 0.5f, tl.y - h * 0.5f };
    };

    auto to_top_left = [w, h](Vec2 c) -> Vec2 {
        return { c.x + w * 0.5f, c.y + h * 0.5f };
    };

    Vec2 center_tl{960.0f, 540.0f};
    Vec2 center_c = to_centered(center_tl);
    CHECK(std::abs(center_c.x) < 1e-4f);
    CHECK(std::abs(center_c.y) < 1e-4f);

    Vec2 roundtrip_tl = to_top_left(center_c);
    CHECK(std::abs(roundtrip_tl.x - 960.0f) < 1e-4f);
    CHECK(std::abs(roundtrip_tl.y - 540.0f) < 1e-4f);

    Vec2 top_left_c{-960.0f, -540.0f};
    Vec2 top_left_tl = to_top_left(top_left_c);
    CHECK(std::abs(top_left_tl.x) < 1e-4f);
    CHECK(std::abs(top_left_tl.y) < 1e-4f);

    Vec2 bottom_right_c{960.0f, 540.0f};
    Vec2 bottom_right_tl = to_top_left(bottom_right_c);
    CHECK(std::abs(bottom_right_tl.x - 1920.0f) < 1e-4f);
    CHECK(std::abs(bottom_right_tl.y - 1080.0f) < 1e-4f);
}

TEST_CASE("Coordinate Centered vs Top Left - Transform matrix offset") {
    SceneBuilder builder;
    builder.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    builder.layer("3d_layer_offset", [](LayerBuilder& lb) {
        lb.enable_3d(true)
          .position({100.0f, 50.0f, 0.0f})
          .rect("red_rect", {.size={100.0f, 100.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.diagnostics.enabled = true;
    renderer.set_settings(settings);

    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -800.0f};
    camera.zoom = 800.0f;

    auto fb = renderer.render_scene(scene, camera, 1920, 1080, 30.0f);
    REQUIRE(fb != nullptr);

    i32 min_red_x = 9999, max_red_x = -9999;
    i32 min_red_y = 9999, max_red_y = -9999;
    for (i32 y = 0; y < fb->height(); ++y) {
        for (i32 x = 0; x < fb->width(); ++x) {
            Color p = fb->get_pixel(x, y);
            if (p.r > 0.5f) {
                min_red_x = std::min(min_red_x, x);
                max_red_x = std::max(max_red_x, x);
                min_red_y = std::min(min_red_y, y);
                max_red_y = std::max(max_red_y, y);
            }
        }
    }
    spdlog::info("RED PIXELS BBOX: [{}, {} -> {}, {}]", min_red_x, min_red_y, max_red_x, max_red_y);

    // Camera2_5D uses screen-Y-down coordinates: authored y=50 maps to
    // canvas y=540-50=490.
    Color p_center = fb->get_pixel(1060, 490);
    Color p_old_center = fb->get_pixel(960, 540);

    CHECK(p_center.r > 0.9f);
    CHECK(p_center.a > 0.9f);
    CHECK(p_old_center.a < 0.05f);
}

TEST_CASE("Coordinate Centered vs Top Left - Layer near border should not disappear") {
    SceneBuilder builder;
    builder.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    builder.layer("3d_border_layer", [](LayerBuilder& lb) {
        lb.enable_3d(true)
          .position({910.0f, 490.0f, 0.0f})
          .rect("red_rect", {.size={100.0f, 100.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.diagnostics.enabled = false;
    renderer.set_settings(settings);

    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -800.0f};
    camera.zoom = 800.0f;

    auto fb = renderer.render_scene(scene, camera, 1920, 1080, 30.0f);
    REQUIRE(fb != nullptr);

    // Camera2_5D flips authored Y around the canvas center; the border layer
    // at y=490 therefore projects to the top edge of the viewport.
    Color p_visible = fb->get_pixel(1850, 50);
    CHECK(p_visible.r > 0.9f);
    CHECK(p_visible.a > 0.9f);
}

TEST_CASE("Coordinate Centered vs Top Left - Render graph mixed 2D and centered") {
    SceneBuilder builder;
    builder.ambient_light(Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
    builder.layer("2d_layer", [](LayerBuilder& lb) {
        lb.rect("red_rect", {.size={100.0f, 100.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    builder.layer("3d_layer", [](LayerBuilder& lb) {
        lb.enable_3d(true)
          .rect("blue_rect", {.size={100.0f, 100.0f}, .color=Color::blue(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.diagnostics.enabled = false;
    renderer.set_settings(settings);

    Camera2_5D camera;
    camera.enabled = true;
    camera.position = {0.0f, 0.0f, -800.0f};
    camera.zoom = 800.0f;

    auto fb = renderer.render_scene(scene, camera, 1920, 1080, 30.0f);
    REQUIRE(fb != nullptr);

    // In the modular coordinate system both layers are centered; a pixel
    // outside the 2D rect should be transparent, while the 3D rect at the
    // canvas center should be visible (and on top of the 2D red rect).
    Color p2d = fb->get_pixel(0, 0);
    Color p3d = fb->get_pixel(960, 540);

    CHECK(p2d.a < 0.05f);
    // The projected blue layer is composited above the centered red layer.
    CHECK(p3d.b > 0.9f);
    CHECK(p3d.a > 0.9f);
}

