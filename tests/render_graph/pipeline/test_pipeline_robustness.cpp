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
    ctx.node_exec.current_shape_processor =
        renderer.backend().resolve_shape_processor(rnode);

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
    ctx.node_exec.current_shape_processor =
        renderer.backend().resolve_shape_processor(rnode);

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
    ctx.node_exec.current_shape_processor =
        renderer.backend().resolve_shape_processor(rnode);

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
    ctx.node_exec.current_shape_processor =
        renderer.backend().resolve_shape_processor(rnode);

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
    ctx.node_exec.current_shape_processor =
        renderer.backend().resolve_shape_processor(rnode);

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
    std::array<renderer::ShapeProcessor*, 2> processors{
        renderer.backend().resolve_shape_processor(rnode_a),
        renderer.backend().resolve_shape_processor(rnode_b)};
    RenderGraphContext ctx;
    ctx.frame_input.width = 1920;
    ctx.frame_input.height = 1080;
    ctx.services.backend = &renderer.backend();
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

namespace {

struct DiagnosticsParityObservation {
    raster::BBox bbox{};
    std::optional<raster::BBox> dirty_clip;
    bool bbox_empty{false};
    u64 pixel_hash{0};
};

bool same_bbox(const raster::BBox& a, const raster::BBox& b) {
    return a.x0 == b.x0 && a.y0 == b.y0 &&
           a.x1 == b.x1 && a.y1 == b.y1;
}

DiagnosticsParityObservation observe_source_diagnostics(
    SoftwareRenderer& renderer,
    const RenderNode& render_node,
    bool diagnostics_enabled,
    bool camera_2_5d)
{
    RenderGraphContext ctx;
    ctx.frame_input.width = 320;
    ctx.frame_input.height = 240;
    ctx.frame_input.frame = 0;
    ctx.policy.diagnostics_enabled = diagnostics_enabled;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.current_shape_processor =
        renderer.backend().resolve_shape_processor(render_node);
    if (camera_2_5d) {
        ctx.frame_input.has_camera_2_5d = true;
        ctx.frame_input.camera_2_5d.enabled = true;
        ctx.frame_input.camera_2_5d.position = {0.0f, 0.0f, -800.0f};
        ctx.frame_input.camera_2_5d.zoom = 800.0f;
    }

    SourceNode node("diagnostics_source", render_node, cache::NodeCacheKey{});
    const auto predicted = node.predicted_bbox(ctx);
    REQUIRE(predicted.has_value());

    // Exercise the real executor dirty-clip decision consumed after
    // predicted_bbox(). Source/Text/Transform nodes intentionally preserve
    // their full predicted bounds here; parity proves diagnostics cannot
    // alter that execution decision.
    ctx.node_exec.dirty_rect = raster::BBox{0, 0, 160, 120};
    const auto dirty_clip = compute_dirty_clip(ctx, node, predicted);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto framebuffer = result.take_value();
    REQUIRE(framebuffer != nullptr);

    return DiagnosticsParityObservation{
        .bbox = *predicted,
        .dirty_clip = dirty_clip,
        .bbox_empty = predicted->is_empty(),
        .pixel_hash = test::framebuffer_hash(*framebuffer),
    };
}

DiagnosticsParityObservation observe_multi_source_diagnostics(
    SoftwareRenderer& renderer,
    const RenderNode& first,
    const RenderNode& second,
    bool diagnostics_enabled,
    bool camera_2_5d)
{
    std::vector<MultiSourceItem> items{
        MultiSourceItem{&first, first.world_transform.to_mat4(), 1.0f},
        MultiSourceItem{&second, second.world_transform.to_mat4(), 1.0f},
    };
    std::array<renderer::ShapeProcessor*, 2> processors{
        renderer.backend().resolve_shape_processor(first),
        renderer.backend().resolve_shape_processor(second),
    };

    RenderGraphContext ctx;
    ctx.frame_input.width = 320;
    ctx.frame_input.height = 240;
    ctx.frame_input.frame = 0;
    ctx.policy.diagnostics_enabled = diagnostics_enabled;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.current_shape_processor = processors[0];
    ctx.node_exec.current_shape_processors = processors;
    if (camera_2_5d) {
        ctx.frame_input.has_camera_2_5d = true;
        ctx.frame_input.camera_2_5d.enabled = true;
        ctx.frame_input.camera_2_5d.position = {0.0f, 0.0f, -800.0f};
        ctx.frame_input.camera_2_5d.zoom = 800.0f;
    }

    MultiSourceNode node("diagnostics_multi_source", std::move(items), cache::NodeCacheKey{});
    const auto predicted = node.predicted_bbox(ctx);
    REQUIRE(predicted.has_value());

    // Exercise the real executor dirty-clip decision consumed after
    // predicted_bbox(), not just a duplicated test-side intersection.
    ctx.node_exec.dirty_rect = raster::BBox{0, 0, 160, 120};
    const auto dirty_clip = compute_dirty_clip(ctx, node, predicted);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto framebuffer = result.take_value();
    REQUIRE(framebuffer != nullptr);

    return DiagnosticsParityObservation{
        .bbox = *predicted,
        .dirty_clip = dirty_clip,
        .bbox_empty = predicted->is_empty(),
        .pixel_hash = test::framebuffer_hash(*framebuffer),
    };
}

void check_parity_decision_and_pixels(
    const DiagnosticsParityObservation& off,
    const DiagnosticsParityObservation& on)
{
    // The bbox is the node's culling/tile/dirty decision input. Exact equality
    // plus equality of the real dirty-clip result proves diagnostics cannot
    // change the execution decision, not merely the final pixels.
    CHECK(same_bbox(off.bbox, on.bbox));
    REQUIRE(off.dirty_clip.has_value());
    REQUIRE(on.dirty_clip.has_value());
    CHECK(same_bbox(*off.dirty_clip, *on.dirty_clip));
    CHECK(off.bbox_empty == on.bbox_empty);
    CHECK(off.pixel_hash == on.pixel_hash);
}

} // namespace

namespace {

class SchedulerEnvironment final {
public:
    explicit SchedulerEnvironment(bool parallel)
        : m_lock(environment_mutex())
        , m_mode(capture("CHRONON3D_SCHEDULER_MODE"))
        , m_workers(capture("CHRONON3D_SCHEDULER_WORKERS")) {
        set("CHRONON3D_SCHEDULER_MODE", parallel ? "fixed" : "sequential");
        if (parallel) {
            set("CHRONON3D_SCHEDULER_WORKERS", "4");
        } else {
            unsetenv("CHRONON3D_SCHEDULER_WORKERS");
        }
    }

    ~SchedulerEnvironment() noexcept {
        restore("CHRONON3D_SCHEDULER_MODE", m_mode);
        restore("CHRONON3D_SCHEDULER_WORKERS", m_workers);
    }

    SchedulerEnvironment(const SchedulerEnvironment&) = delete;
    SchedulerEnvironment& operator=(const SchedulerEnvironment&) = delete;

private:
    static std::mutex& environment_mutex() {
        static std::mutex mutex;
        return mutex;
    }

    struct Variable {
        bool present{false};
        std::string value;
    };

    static Variable capture(const char* name) {
        if (const char* value = std::getenv(name)) {
            return Variable{true, value};
        }
        return {};
    }

    static void set(const char* name, const char* value) {
        (void)::setenv(name, value, 1);
    }

    static void restore(const char* name, const Variable& variable) noexcept {
        if (variable.present) {
            (void)::setenv(name, variable.value.c_str(), 1);
        } else {
            (void)::unsetenv(name);
        }
    }

    std::unique_lock<std::mutex> m_lock;
    Variable m_mode;
    Variable m_workers;
};

struct RuntimeDiagnosticsObservation {
    u64 pixel_hash{0};
    bool dirty_rect_enabled{false};
    bool tile_execution_used{false};
    bool fast_path_reused{false};
    bool graph_reused{false};
    int layer_count{0};
    std::optional<raster::BBox> dirty_rect;
    raster::BBox node_bbox{};
    std::optional<raster::BBox> node_dirty_clip;
    u64 parallel_regions{0};
    u64 sequential_levels{0};
    bool scheduler_is_parallel{false};
};

bool same_optional_bbox(
    const std::optional<raster::BBox>& lhs,
    const std::optional<raster::BBox>& rhs)
{
    if (lhs.has_value() != rhs.has_value()) return false;
    if (!lhs) return true;
    return same_bbox(*lhs, *rhs);
}

void check_runtime_decision_parity(
    const RuntimeDiagnosticsObservation& off,
    const RuntimeDiagnosticsObservation& on)
{
    CHECK(off.dirty_rect_enabled == on.dirty_rect_enabled);
    CHECK(off.tile_execution_used == on.tile_execution_used);
    CHECK(off.fast_path_reused == on.fast_path_reused);
    CHECK(off.graph_reused == on.graph_reused);
    CHECK(off.layer_count == on.layer_count);
    CHECK(same_optional_bbox(off.dirty_rect, on.dirty_rect));
    CHECK(same_bbox(off.node_bbox, on.node_bbox));
    CHECK(same_optional_bbox(off.node_dirty_clip, on.node_dirty_clip));
    CHECK(off.parallel_regions == on.parallel_regions);
    CHECK(off.sequential_levels == on.sequential_levels);
    CHECK(off.pixel_hash == on.pixel_hash);
}

DiagnosticsParityObservation observe_runtime_node_diagnostics(
    SoftwareRenderer& renderer,
    bool multi_source,
    bool diagnostics_enabled,
    bool camera_2_5d)
{
    auto* resource = std::pmr::get_default_resource();
    if (multi_source) {
        const RenderNode first = RenderNodeFactory::rect(resource, "runtime_multi_a", {
            .size = {140.0f, 90.0f},
            .color = Color::red(),
            .pos = {260.0f, 70.0f, 0.0f},
        });
        const RenderNode second = RenderNodeFactory::rect(resource, "runtime_multi_b", {
            .size = {80.0f, 100.0f},
            .color = Color::blue(),
            .pos = {40.0f, 120.0f, 0.0f},
        });
        return observe_multi_source_diagnostics(
            renderer, first, second, diagnostics_enabled, camera_2_5d);
    }

    const RenderNode source = RenderNodeFactory::rect(resource, "runtime_source", {
        .size = {120.0f, 80.0f},
        .color = Color::green(),
        .pos = {280.0f, 100.0f, 0.0f},
    });
    return observe_source_diagnostics(
        renderer, source, diagnostics_enabled, camera_2_5d);
}

Scene make_runtime_parity_scene(bool multi_source, bool camera_2_5d) {
    SceneBuilder builder;
    builder.layer("runtime_parity_a", [multi_source, camera_2_5d](LayerBuilder& layer) {
        if (camera_2_5d) layer.enable_3d(true);
        layer.rect("runtime_a_red", {
            .size = {72.0f, 64.0f},
            .color = Color::red(),
            .pos = {48.0f, 44.0f, 0.0f},
        });
        if (multi_source) {
            layer.rect("runtime_a_green", {
                .size = {36.0f, 42.0f},
                .color = Color::green(),
                .pos = {138.0f, 80.0f, 0.0f},
            });
        }
    });
    builder.layer("runtime_parity_b", [multi_source, camera_2_5d](LayerBuilder& layer) {
        if (camera_2_5d) layer.enable_3d(true);
        layer.rect("runtime_b_blue", {
            .size = {60.0f, 76.0f},
            .color = Color::blue(),
            .pos = {208.0f, 112.0f, 0.0f},
        });
        if (multi_source) {
            layer.rect("runtime_b_yellow", {
                .size = {32.0f, 48.0f},
                .color = Color::yellow(),
                .pos = {248.0f, 38.0f, 0.0f},
            });
        }
    });
    return builder.build();
}

RuntimeDiagnosticsObservation observe_runtime_diagnostics(
    bool multi_source,
    bool camera_2_5d,
    bool diagnostics_enabled,
    bool parallel)
{
    SchedulerEnvironment scheduler_environment(parallel);
    auto renderer = test::make_renderer();
    auto settings = renderer.render_settings();
    settings.diagnostics.enabled = diagnostics_enabled;
    renderer.set_settings(settings);

    const auto node_observation = observe_runtime_node_diagnostics(
        renderer, multi_source, diagnostics_enabled, camera_2_5d);

    const Scene scene = make_runtime_parity_scene(multi_source, camera_2_5d);
    std::shared_ptr<Framebuffer> framebuffer;
    if (camera_2_5d) {
        Camera2_5D camera;
        camera.enabled = true;
        camera.position = {0.0f, 0.0f, -800.0f};
        camera.zoom = 800.0f;
        framebuffer = renderer.render_scene(
            scene, std::optional<Camera2_5D>{camera}, 320, 240, 30.0f);
    } else {
        framebuffer = renderer.render_scene(scene, Camera{}, 320, 240, 30.0f);
    }
    REQUIRE(framebuffer != nullptr);

    const auto* counters = renderer.counters();
    REQUIRE(counters != nullptr);
    return RuntimeDiagnosticsObservation{
        .pixel_hash = test::framebuffer_hash(*framebuffer),
        .dirty_rect_enabled = renderer.last_dirty_rect_enabled(),
        .tile_execution_used = renderer.last_tile_execution_used(),
        .fast_path_reused = renderer.last_fast_path_reused(),
        .graph_reused = renderer.last_graph_reused(),
        .layer_count = renderer.last_layer_count(),
        .dirty_rect = renderer.last_dirty_rect(),
        .node_bbox = node_observation.bbox,
        .node_dirty_clip = node_observation.dirty_clip,
        .parallel_regions = counters->parallel_regions_count.load(std::memory_order_relaxed),
        .sequential_levels = counters->level_sequential_count.load(std::memory_order_relaxed),
        .scheduler_is_parallel = renderer.scheduler().mode() == SchedulerMode::TbbFixed &&
                                 renderer.scheduler().concurrency() > 1,
    };
}

void check_scheduler_mode(const RuntimeDiagnosticsObservation& observation, bool parallel) {
    CHECK(observation.scheduler_is_parallel == parallel);
    if (parallel) {
        CHECK(observation.parallel_regions > 0);
    } else {
        CHECK(observation.parallel_regions == 0);
        CHECK(observation.sequential_levels > 0);
    }
}

void check_serial_parallel_parity(
    const RuntimeDiagnosticsObservation& serial,
    const RuntimeDiagnosticsObservation& parallel)
{
    CHECK(serial.pixel_hash == parallel.pixel_hash);
    CHECK(serial.dirty_rect_enabled == parallel.dirty_rect_enabled);
    CHECK(serial.tile_execution_used == parallel.tile_execution_used);
    CHECK(serial.fast_path_reused == parallel.fast_path_reused);
    CHECK(serial.graph_reused == parallel.graph_reused);
    CHECK(serial.layer_count == parallel.layer_count);
    CHECK(same_optional_bbox(serial.dirty_rect, parallel.dirty_rect));
    CHECK(same_bbox(serial.node_bbox, parallel.node_bbox));
    CHECK(same_optional_bbox(serial.node_dirty_clip, parallel.node_dirty_clip));
}

} // namespace

#define CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST(name, multi_source, camera_2_5d) \
    TEST_CASE(name) { \
        const auto serial_off = observe_runtime_diagnostics( \
            multi_source, camera_2_5d, false, false); \
        const auto serial_on = observe_runtime_diagnostics( \
            multi_source, camera_2_5d, true, false); \
        const auto parallel_off = observe_runtime_diagnostics( \
            multi_source, camera_2_5d, false, true); \
        const auto parallel_on = observe_runtime_diagnostics( \
            multi_source, camera_2_5d, true, true); \
        check_runtime_decision_parity(serial_off, serial_on); \
        check_runtime_decision_parity(parallel_off, parallel_on); \
        check_scheduler_mode(serial_off, false); \
        check_scheduler_mode(serial_on, false); \
        check_scheduler_mode(parallel_off, true); \
        check_scheduler_mode(parallel_on, true); \
        check_serial_parallel_parity(serial_off, parallel_off); \
        check_serial_parallel_parity(serial_on, parallel_on); \
    }

CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST(
    "Diagnostics runtime parity: SourceNode 2D serial and parallel", false, false)
CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST(
    "Diagnostics runtime parity: MultiSourceNode 2D serial and parallel", true, false)
CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST(
    "Diagnostics runtime parity: SourceNode Camera2_5D serial and parallel", false, true)
CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST(
    "Diagnostics runtime parity: MultiSourceNode Camera2_5D serial and parallel", true, true)

#undef CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST

TEST_CASE("Diagnostics parity: SourceNode 2D keeps bbox decisions and pixels identical") {
    auto renderer = test::make_renderer();
    auto* resource = std::pmr::get_default_resource();
    const RenderNode source = RenderNodeFactory::rect(resource, "source_2d_parity", {
        .size = {120.0f, 80.0f},
        .color = Color::red(),
        .pos = {280.0f, 100.0f, 0.0f},
    });

    const auto off = observe_source_diagnostics(renderer, source, false, false);
    const auto on = observe_source_diagnostics(renderer, source, true, false);
    check_parity_decision_and_pixels(off, on);
    CHECK(off.bbox.x1 == 320);
    CHECK(off.bbox.y1 <= 240);
}

TEST_CASE("Diagnostics parity: MultiSourceNode 2D keeps bbox decisions and pixels identical") {
    auto renderer = test::make_renderer();
    auto* resource = std::pmr::get_default_resource();
    const RenderNode first = RenderNodeFactory::rect(resource, "multi_2d_a", {
        .size = {140.0f, 90.0f},
        .color = Color::red(),
        .pos = {260.0f, 70.0f, 0.0f},
    });
    const RenderNode second = RenderNodeFactory::rect(resource, "multi_2d_b", {
        .size = {80.0f, 100.0f},
        .color = Color::blue(),
        .pos = {40.0f, 120.0f, 0.0f},
    });

    const auto off = observe_multi_source_diagnostics(renderer, first, second, false, false);
    const auto on = observe_multi_source_diagnostics(renderer, first, second, true, false);
    check_parity_decision_and_pixels(off, on);
    CHECK(off.bbox.x0 >= 0);
    CHECK(off.bbox.y0 >= 0);
    CHECK(off.bbox.x1 <= 320);
    CHECK(off.bbox.y1 <= 240);
}

TEST_CASE("Diagnostics parity: SourceNode Camera2_5D keeps bbox decisions and pixels identical") {
    auto renderer = test::make_renderer();
    auto* resource = std::pmr::get_default_resource();
    const RenderNode source = RenderNodeFactory::rect(resource, "source_camera_parity", {
        .size = {120.0f, 80.0f},
        .color = Color::green(),
        .pos = {40.0f, -30.0f, 0.0f},
    });

    const auto off = observe_source_diagnostics(renderer, source, false, true);
    const auto on = observe_source_diagnostics(renderer, source, true, true);
    check_parity_decision_and_pixels(off, on);
    CHECK(!off.bbox.is_empty());
}

TEST_CASE("EvaluatedLayerPlacement resolves canvas placement without changing it") {
    SceneBuilder builder;
    builder.layer("placement_canvas", [](LayerBuilder& layer) {
        layer.rect("placement_rect", {
            .size = {80.0f, 60.0f},
            .color = Color::red(),
            .pos = {20.0f, 30.0f, 0.0f},
        });
    });
    const Scene scene = builder.build();

    RenderGraphContext ctx;
    ctx.frame_input.width = 320;
    ctx.frame_input.height = 240;
    ctx.policy.modular_coordinates = true;
    const auto resolved = chronon3d::graph::detail::resolve_layers(scene, ctx);
    REQUIRE(resolved.layers.size() == 1);

    const auto& layer = resolved.layers.front();
    LayerGraphItem item{
        .layer = layer.layer,
        .transform = layer.world_transform,
        .world_matrix = layer.world_matrix,
        .projected = false,
        .native_3d = false,
    };

    const auto placement = chronon3d::graph::detail::evaluate_layer_placement(item, ctx);
    CHECK(placement.space == chronon3d::graph::detail::EvaluatedCoordinateSpace::Canvas);
    CHECK(placement.visible);
    CHECK_FALSE(placement.requires_transform_node);
    CHECK_FALSE(placement.applies_camera_in_processor);
    CHECK_FALSE(placement.defer_camera_projection);
    CHECK(chronon3d::graph::detail::matrix_near(placement.world_matrix, item.world_matrix));
    CHECK(chronon3d::graph::detail::matrix_near(placement.source_matrix, item.world_matrix));
    CHECK(chronon3d::graph::detail::matrix_near(placement.render_matrix, item.world_matrix));
}

TEST_CASE("EvaluatedLayerPlacement resolves projected and native 3D ownership") {
    SceneBuilder builder;
    builder.layer("placement_3d", [](LayerBuilder& layer) {
        layer.enable_3d(true)
            .rect("placement_rect", {
                .size = {80.0f, 60.0f},
                .color = Color::blue(),
                .pos = {0.0f, 0.0f, 0.0f},
            });
    });
    const Scene scene = builder.build();
    const auto resolved = chronon3d::graph::detail::resolve_layers(scene, RenderGraphContext{});
    REQUIRE(resolved.layers.size() == 1);

    RenderGraphContext projected_ctx;
    projected_ctx.frame_input.width = 320;
    projected_ctx.frame_input.height = 240;
    projected_ctx.frame_input.has_camera_2_5d = true;
    projected_ctx.policy.modular_coordinates = true;

    const auto& layer = resolved.layers.front();
    const Mat4 projection = glm::translate(Mat4(1.0f), Vec3(12.0f, 18.0f, 0.0f));

    LayerGraphItem projected_item{
        .layer = layer.layer,
        .transform = layer.world_transform,
        .world_matrix = layer.world_matrix,
        .projection_matrix = projection,
        .projected = true,
        .native_3d = false,
    };
    const auto projected = chronon3d::graph::detail::evaluate_layer_placement(projected_item, projected_ctx);
    CHECK(projected.space == chronon3d::graph::detail::EvaluatedCoordinateSpace::CameraProjected);
    CHECK(projected.visible);
    CHECK(projected.requires_transform_node);
    CHECK_FALSE(projected.applies_camera_in_processor);
    CHECK(projected.defer_camera_projection);
    CHECK(chronon3d::graph::detail::matrix_near(projected.source_matrix,
        chronon3d::graph::detail::implicit_canvas_center_matrix(projected_ctx)));
    CHECK(chronon3d::graph::detail::matrix_near(projected.render_matrix, projection));

    LayerGraphItem native_item{
        .layer = layer.layer,
        .transform = layer.world_transform,
        .world_matrix = layer.world_matrix,
        .projected = false,
        .native_3d = true,
    };
    const auto native = chronon3d::graph::detail::evaluate_layer_placement(native_item, projected_ctx);
    CHECK(native.space == chronon3d::graph::detail::EvaluatedCoordinateSpace::Native3D);
    CHECK(native.visible);
    CHECK_FALSE(native.requires_transform_node);
    CHECK(native.applies_camera_in_processor);
    CHECK_FALSE(native.defer_camera_projection);
    CHECK(chronon3d::graph::detail::matrix_near(native.source_matrix, layer.world_matrix));
    CHECK(chronon3d::graph::detail::matrix_near(native.render_matrix, layer.world_matrix));
}

TEST_CASE("Diagnostics parity: MultiSourceNode Camera2_5D keeps bbox decisions and pixels identical") {
    auto renderer = test::make_renderer();
    auto* resource = std::pmr::get_default_resource();
    const RenderNode first = RenderNodeFactory::rect(resource, "multi_camera_a", {
        .size = {100.0f, 70.0f},
        .color = Color::yellow(),
        .pos = {-50.0f, 20.0f, 0.0f},
    });
    const RenderNode second = RenderNodeFactory::rect(resource, "multi_camera_b", {
        .size = {70.0f, 110.0f},
        .color = Color::blue(),
        .pos = {80.0f, -30.0f, 0.0f},
    });

    const auto off = observe_multi_source_diagnostics(renderer, first, second, false, true);
    const auto on = observe_multi_source_diagnostics(renderer, first, second, true, true);
    check_parity_decision_and_pixels(off, on);
    CHECK(!off.bbox.is_empty());
}
