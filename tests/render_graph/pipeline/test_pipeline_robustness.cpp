#include <doctest/doctest.h>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/trace.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/scene/render_node_factory.hpp>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::graph;

TEST_CASE("Coordinate Centered vs Top Left - 2D standard top left layer") {
    SceneBuilder builder;
    builder.layer("2d_layer", [](LayerBuilder& lb) {
        lb.rect("red_rect", {.size={100.0f, 100.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    SoftwareRenderer renderer;
    RenderSettings settings = renderer.settings();
    settings.use_modular_graph = false;
    settings.diagnostic = false;
    renderer.set_settings(settings);

    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 200, 200);
    REQUIRE(fb != nullptr);

    Color p00 = fb->get_pixel(0, 0);
    Color p49 = fb->get_pixel(49, 49);
    Color p50 = fb->get_pixel(50, 50);

    std::fprintf(stderr, "=== DEBUG pixels ===\n");
    std::fprintf(stderr, "p00: r=%f, g=%f, b=%f, a=%f\n", p00.r, p00.g, p00.b, p00.a);
    std::fprintf(stderr, "p49: r=%f, g=%f, b=%f, a=%f\n", p49.r, p49.g, p49.b, p49.a);
    std::fprintf(stderr, "p50: r=%f, g=%f, b=%f, a=%f\n", p50.r, p50.g, p50.b, p50.a);
    std::fprintf(stderr, "====================\n");

    CHECK(p00.r > 0.9f);
    CHECK(p00.g < 0.1f);
    CHECK(p00.b < 0.1f);
    CHECK(p00.a > 0.9f);

    CHECK(p49.r > 0.9f);
    CHECK(p49.g < 0.1f);
    CHECK(p49.b < 0.1f);
    CHECK(p49.a > 0.9f);

    CHECK(p50.a < 0.05f);
}

TEST_CASE("Coordinate Centered vs Top Left - Centered exactly on canvas") {
    SceneBuilder builder;
    builder.camera().enable(true);
    builder.layer("3d_layer", [](LayerBuilder& lb) {
        lb.enable_3d(true)
          .rect("red_rect", {.size={200.0f, 200.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    SoftwareRenderer renderer;
    RenderSettings settings = renderer.settings();
    settings.use_modular_graph = false;
    settings.diagnostic = false;
    renderer.set_settings(settings);

    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 1920, 1080);
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
    builder.camera().enable(true);
    builder.layer("3d_layer_offset", [](LayerBuilder& lb) {
        lb.enable_3d(true)
          .position({100.0f, 50.0f, 0.0f})
          .rect("red_rect", {.size={100.0f, 100.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    SoftwareRenderer renderer;
    RenderSettings settings = renderer.settings();
    settings.use_modular_graph = false;
    settings.diagnostic = false;
    renderer.set_settings(settings);

    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 1920, 1080);
    REQUIRE(fb != nullptr);

    Color p_center = fb->get_pixel(1060, 590);
    Color p_old_center = fb->get_pixel(960, 540);

    CHECK(p_center.r > 0.9f);
    CHECK(p_center.a > 0.9f);
    CHECK(p_old_center.a < 0.05f);
}

TEST_CASE("Coordinate Centered vs Top Left - Layer near border should not disappear") {
    SceneBuilder builder;
    builder.camera().enable(true);
    builder.layer("3d_border_layer", [](LayerBuilder& lb) {
        lb.enable_3d(true)
          .position({910.0f, 490.0f, 0.0f})
          .rect("red_rect", {.size={100.0f, 100.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    SoftwareRenderer renderer;
    RenderSettings settings = renderer.settings();
    settings.use_modular_graph = false;
    settings.diagnostic = false;
    renderer.set_settings(settings);

    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 1920, 1080);
    REQUIRE(fb != nullptr);

    Color p_visible = fb->get_pixel(1850, 1000);
    CHECK(p_visible.r > 0.9f);
    CHECK(p_visible.a > 0.9f);
}

TEST_CASE("Coordinate Centered vs Top Left - Render graph mixed 2D and centered") {
    SceneBuilder builder;
    builder.camera().enable(true);
    builder.layer("2d_layer", [](LayerBuilder& lb) {
        lb.rect("red_rect", {.size={100.0f, 100.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    builder.layer("3d_layer", [](LayerBuilder& lb) {
        lb.enable_3d(true)
          .rect("blue_rect", {.size={100.0f, 100.0f}, .color=Color::blue(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    SoftwareRenderer renderer;
    RenderSettings settings = renderer.settings();
    settings.use_modular_graph = false;
    settings.diagnostic = false;
    renderer.set_settings(settings);

    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 1920, 1080);
    REQUIRE(fb != nullptr);

    Color p2d = fb->get_pixel(0, 0);
    Color p3d = fb->get_pixel(960, 540);

    CHECK(p2d.r > 0.9f);
    CHECK(p2d.a > 0.9f);
    CHECK(p3d.b > 0.9f);
    CHECK(p3d.a > 0.9f);
}

TEST_CASE("Effects, predicted_bbox and clipping - Blur near border doesn't crash") {
    SceneBuilder builder;
    builder.layer("blur_layer", [](LayerBuilder& lb) {
        lb.position({10.0f, 10.0f, 0.0f})
          .blur(30.0f)
          .rect("red_rect", {.size={100.0f, 100.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    SoftwareRenderer renderer;
    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 200, 200);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 200);
}

TEST_CASE("Test visivi e lettura pixel in C++ - Pixel check white") {
    SceneBuilder builder;
    builder.layer("white_rect_layer", [](LayerBuilder& lb) {
        lb.rect("white_rect", {.size={50.0f, 50.0f}, .color=Color::white(), .pos={0.0f, 0.0f, 0.0f}});
    });
    Scene scene = builder.build();

    SoftwareRenderer renderer;
    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 100, 100);
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

    SoftwareRenderer renderer;
    RenderSettings settings = renderer.settings();
    settings.use_modular_graph = false;
    renderer.set_settings(settings);

    Camera camera;

    auto fb = renderer.render_scene(scene, camera, 200, 200);
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

    SoftwareRenderer renderer;
    RenderGraphContext ctx;
    ctx.width = 1920;
    ctx.height = 1080;
    ctx.backend = &renderer;

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key, false, false);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 0);
    CHECK(bbox.y0 == 0);
    CHECK(bbox.x1 == 60);
    CHECK(bbox.y1 == 60);

    auto fb = node.execute(ctx, {}, {});
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

    SoftwareRenderer renderer;
    RenderGraphContext ctx;
    ctx.width = 1920;
    ctx.height = 1080;
    ctx.backend = &renderer;

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key, false, true);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 900);
    CHECK(bbox.y0 == 480);
    CHECK(bbox.x1 == 1020);
    CHECK(bbox.y1 == 600);

    auto fb = node.execute(ctx, {}, {});
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

    SoftwareRenderer renderer;
    RenderGraphContext ctx;
    ctx.width = 1920;
    ctx.height = 1080;
    ctx.backend = &renderer;

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key, true, false);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 900);
    CHECK(bbox.y0 == 480);
    CHECK(bbox.x1 == 1020);
    CHECK(bbox.y1 == 600);

    auto fb = node.execute(ctx, {}, {});
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

    SoftwareRenderer renderer;
    RenderGraphContext ctx;
    ctx.width = 1920;
    ctx.height = 1080;
    ctx.backend = &renderer;

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key, true, true);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 900);
    CHECK(bbox.y0 == 480);
    CHECK(bbox.x1 == 1020);
    CHECK(bbox.y1 == 600);

    auto fb = node.execute(ctx, {}, {});
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);
}

TEST_CASE("SourceNode predicted_bbox vs execute - 3D source near border") {
    auto* res = std::pmr::get_default_resource();
    RenderNode rnode = RenderNodeFactory::rect(res, "my_rect", {
        .size = {100.0f, 100.0f},
        .color = Color::red(),
        .pos = {900.0f, 500.0f, 0.0f}
    });

    SoftwareRenderer renderer;
    RenderGraphContext ctx;
    ctx.width = 1920;
    ctx.height = 1080;
    ctx.backend = &renderer;

    cache::NodeCacheKey key{};
    SourceNode node("my_node", rnode, key, false, true);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 1800);
    CHECK(bbox.y0 == 980);
    CHECK(bbox.x1 == 1920);
    CHECK(bbox.y1 == 1080);

    auto fb = node.execute(ctx, {}, {});
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

    SoftwareRenderer renderer;
    RenderGraphContext ctx;
    ctx.width = 1920;
    ctx.height = 1080;
    ctx.backend = &renderer;

    cache::NodeCacheKey key{};
    MultiSourceNode node("my_multi_node", std::move(items), key, false, true);

    auto opt_bbox = node.predicted_bbox(ctx);
    REQUIRE(opt_bbox.has_value());
    auto bbox = *opt_bbox;

    CHECK(bbox.x0 == 900);
    CHECK(bbox.y0 == 480);
    CHECK(bbox.x1 == 1220);
    CHECK(bbox.y1 == 600);

    auto fb = node.execute(ctx, {}, {});
    REQUIRE(fb != nullptr);

    int outside = count_alpha_outside_bbox(*fb, expand_bbox(bbox, 2));
    CHECK(outside == 0);

    int inside = count_alpha_inside_bbox(*fb, bbox);
    CHECK(inside > 0);

    Color p_a = fb->get_pixel(960, 540);
    Color p_b = fb->get_pixel(1160, 540);

    CHECK(p_a.r > 0.9f);
    CHECK(p_a.g < 0.1f);
    CHECK(p_b.g > 0.9f);
    CHECK(p_b.r < 0.1f);
}

