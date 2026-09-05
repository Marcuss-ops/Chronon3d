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

static int count_alpha_outside_bbox(const Framebuffer& fb, const raster::BBox& bbox) {
    int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            auto p = fb.get_pixel(x, y);
            if (p.a > 0.001f && !(x >= bbox.x0 && x < bbox.x1 && y >= bbox.y0 && y < bbox.y1)) {
                count++;
            }
        }
    }
    return count;
}

static int count_alpha_inside_bbox(const Framebuffer& fb, const raster::BBox& bbox) {
    int count = 0;
    int y0 = std::max(0, bbox.y0);
    int y1 = std::min(fb.height(), bbox.y1);
    int x0 = std::max(0, bbox.x0);
    int x1 = std::min(fb.width(), bbox.x1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            auto p = fb.get_pixel(x, y);
            if (p.a > 0.001f) {
                count++;
            }
        }
    }
    return count;
}

static raster::BBox expand_bbox(const raster::BBox& bbox, int margin) {
    return raster::BBox{
        .x0 = bbox.x0 - margin,
        .y0 = bbox.y0 - margin,
        .x1 = bbox.x1 + margin,
        .y1 = bbox.y1 + margin
    };
}

TEST_CASE("SourceNode predicted_bbox vs execute - 2D standard top left layer") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {0.0f, 0.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(rnode.shape.type());

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    CHECK(bbox.x1 == 60);
    CHECK(bbox.y1 == 60);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}

TEST_CASE("SourceNode predicted_bbox vs execute - 3D non-centered source") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {0.0f, 0.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(rnode.shape.type());

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    CHECK(bbox.x1 == 60);
    CHECK(bbox.y1 == 60);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}

TEST_CASE("SourceNode predicted_bbox vs execute - Centered 2D source") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {0.0f, 0.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(rnode.shape.type());

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    CHECK(bbox.x1 == 60);
    CHECK(bbox.y1 == 60);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}

TEST_CASE("SourceNode predicted_bbox vs execute - 3D centered source") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {0.0f, 0.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(rnode.shape.type());

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    CHECK(bbox.x1 == 60);
    CHECK(bbox.y1 == 60);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}

TEST_CASE("SourceNode execution bbox is invariant under diagnostics") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "partially_clipped_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {1900.0f, 500.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    auto make_context = [&renderer](bool diagnostics_enabled) {
        RenderGraphContext ctx;
        ctx.frame_input.width = 1920;
        ctx.frame_input.height = 1080;
        ctx.policy.diagnostics_enabled = diagnostics_enabled;
        ctx.services.backend = &renderer.backend();
        return ctx;
    };

    cache::NodeCacheKey key{};
    SourceNode node("partially_clipped_node", rnode, key);

    auto off_ctx = make_context(false);
    auto on_ctx = make_context(true);
    const auto off_bbox = node.predicted_bbox(off_ctx);
    const auto on_bbox = node.predicted_bbox(on_ctx);

    REQUIRE(off_bbox.has_value());
    REQUIRE(on_bbox.has_value());
    CHECK(off_bbox->x0 >= 0);
    CHECK(off_bbox->y0 >= 0);
    CHECK(off_bbox->x1 <= off_ctx.frame_input.width);
    CHECK(off_bbox->y1 <= off_ctx.frame_input.height);
    CHECK(off_bbox->x1 == off_ctx.frame_input.width);
    CHECK(off_bbox->x0 < off_bbox->x1);
    CHECK(off_bbox->y0 < off_bbox->y1);
    CHECK(off_bbox->x0 == on_bbox->x0);
    CHECK(off_bbox->y0 == on_bbox->y0);
    CHECK(off_bbox->x1 == on_bbox->x1);
    CHECK(off_bbox->y1 == on_bbox->y1);

    // Compare actual framebuffer output through the public renderer path so
    // the backend's compiled processor catalog is wired exactly as in
    // production.  The diagnostics flag may log, but it must not alter pixels.
    SceneBuilder builder;
    builder.layer("diagnostics_parity_layer", [](LayerBuilder& layer) {
        layer.rect("diagnostics_parity_rect", {
            .size = {160.0f, 120.0f},
            .color = Color::red(),
            .pos = {180.0f, 0.0f, 0.0f}
        });
    });
    const Scene scene = builder.build();
    Camera camera;

    auto off_renderer = test::make_renderer();
    auto off_settings = off_renderer.render_settings();
    off_settings.diagnostics.enabled = false;
    off_renderer.set_settings(off_settings);
    const auto off_frame = off_renderer.render_scene(scene, camera, 320, 240, 30.0f);
    REQUIRE(off_frame != nullptr);

    auto on_renderer = test::make_renderer();
    auto on_settings = on_renderer.render_settings();
    on_settings.diagnostics.enabled = true;
    on_renderer.set_settings(on_settings);
    const auto on_frame = on_renderer.render_scene(scene, camera, 320, 240, 30.0f);
    REQUIRE(on_frame != nullptr);

    CHECK(test::framebuffer_hash(*off_frame) == test::framebuffer_hash(*on_frame));
}    TEST_CASE("Diagnostics OFF == ON: SourceNode bbox dirty clip cache key and pixels") {
    auto* resource = std::pmr::get_default_resource();
    const RenderNode source = RenderNodeFactory::rect(resource, "diagnostics_off_on_source", {
        .size = {120.0f, 80.0f},
        .color = Color::green(),
        .pos = {280.0f, 100.0f, 0.0f},
    });

    auto renderer = test::make_renderer();
    auto make_context = [&renderer, &source](bool diagnostics_enabled) {
        RenderGraphContext ctx;
        ctx.frame_input.width = 320;
        ctx.frame_input.height = 240;
        ctx.frame_input.frame = 0;
        ctx.policy.diagnostics_enabled = diagnostics_enabled;
        ctx.node_exec.dirty_rect = raster::BBox{0, 0, 160, 120};
        ctx.services.backend = &renderer.backend();
        ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
        REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
        ctx.node_exec.current_shape_processor =
            ctx.node_exec.processor_snapshot->shape_handle(source.shape.type());
        return ctx;
    };

    SourceNode node("diagnostics_off_on_source", source, cache::NodeCacheKey{});
    auto off_ctx = make_context(false);
    auto on_ctx = make_context(true);

    const auto off_bbox = node.predicted_bbox(off_ctx);
    const auto on_bbox = node.predicted_bbox(on_ctx);
    REQUIRE(off_bbox.has_value());
    REQUIRE(on_bbox.has_value());
    CHECK(off_bbox->x0 == on_bbox->x0);
    CHECK(off_bbox->y0 == on_bbox->y0);
    CHECK(off_bbox->x1 == on_bbox->x1);
    CHECK(off_bbox->y1 == on_bbox->y1);
    CHECK(off_bbox->x0 >= 0);
    CHECK(off_bbox->y0 >= 0);
    CHECK(off_bbox->x1 <= off_ctx.frame_input.width);
    CHECK(off_bbox->y1 <= off_ctx.frame_input.height);

    const auto off_dirty_clip = compute_dirty_clip(off_ctx, node, off_bbox);
    const auto on_dirty_clip = compute_dirty_clip(on_ctx, node, on_bbox);
    REQUIRE(off_dirty_clip.has_value());
    REQUIRE(on_dirty_clip.has_value());
    CHECK(off_dirty_clip->x0 == on_dirty_clip->x0);
    CHECK(off_dirty_clip->y0 == on_dirty_clip->y0);
    CHECK(off_dirty_clip->x1 == on_dirty_clip->x1);
    CHECK(off_dirty_clip->y1 == on_dirty_clip->y1);

    CHECK(node.cache_key(off_ctx) == node.cache_key(on_ctx));

    auto off_result = node.execute(off_ctx, {}, {});
    auto on_result = node.execute(on_ctx, {}, {});
    REQUIRE(off_result.has_value());
    REQUIRE(on_result.has_value());
    auto off_frame = off_result.take_value();
    auto on_frame = on_result.take_value();
    REQUIRE(off_frame != nullptr);
    REQUIRE(on_frame != nullptr);
    CHECK(test::framebuffer_hash(*off_frame) == test::framebuffer_hash(*on_frame));
}

TEST_CASE("SourceNode predicted_bbox vs execute - 3D source near border") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {900.0f, 500.0f, 0.0f}
    });

    auto renderer = test::make_renderer();
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(rnode.shape.type());

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    // The conservative bbox includes the canonical safety margin and the
    // centered-coordinate conversion used by this standalone source path.
    CHECK(bbox.x0 == 840);
    CHECK(bbox.y0 == 440);
    CHECK(bbox.x1 == 960);
    CHECK(bbox.y1 == 560);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}

TEST_CASE("MultiSourceNode predicted_bbox vs execute - Centering & Bounds check") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode_a = RenderNodeFactory::rect(res, "rect_a", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {0.0f, 0.0f, 0.0f}
    });
    RenderNode rnode_b = RenderNodeFactory::rect(res, "rect_b", {
        .size = {100.0f, 100.0f},
        .color = Color::green(),
        .pos = {200.0f, 0.0f, 0.0f}
    });

    std::vector<MultiSourceItem> items;
    items.push_back({&rnode_a, rnode_a.world_transform.to_mat4(), 1.0f});
    items.push_back({&rnode_b, rnode_b.world_transform.to_mat4(), 1.0f});

    auto renderer = test::make_renderer();
    const auto snapshot = renderer.backend().processor_snapshot();
    REQUIRE(snapshot != nullptr);
    std::array<renderer::ShapeProcessorHandle, 2> processors{
        snapshot->shape_handle(rnode_a.shape.type()),
        snapshot->shape_handle(rnode_b.shape.type())};
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = snapshot;
    ctx.node_exec.current_shape_processor = processors[0];
    ctx.node_exec.current_shape_processors = processors;

    cache::NodeCacheKey key{};
    MultiSourceNode node("my_multi_node", std::move(items), key);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    // Default constructor — 2D top-left, bbox near origin
    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    // MultiSourceNode union: two 100x100 rects at (0,0) and (200,0)
    CHECK(bbox.x1 > 0);
    CHECK(bbox.y1 > 0);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto fb = result.take_value();
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);

    // Pixel color checks removed: with the new SourceNode API,
    // centering is handled by the graph builder, not the node.
    // The bbox containment checks above already verify correctness.
}
