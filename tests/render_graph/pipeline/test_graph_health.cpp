#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <cmath>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;

namespace {

constexpr float kEps = 1e-3f;

std::shared_ptr<Framebuffer> render_graph_scene(
    SoftwareRenderer& renderer,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    int width,
    int height
) {
    return render_scene_via_graph(
        renderer.backend(),
        node_cache,
        scene,
        camera,
        width,
        height,
        0,
        0.0f,
        renderer.render_settings(),
        renderer.composition_registry(),
        renderer.video_decoder(),
        30.0f
    );
}

bool pixel_near(const Color& a, const Color& b, float eps = kEps) {
    return std::abs(a.r - b.r) <= eps &&
           std::abs(a.g - b.g) <= eps &&
           std::abs(a.b - b.b) <= eps &&
           std::abs(a.a - b.a) <= eps;
}

void check_pixel_matches(const Framebuffer& fb, int x, int y, const Color& expected, float eps = kEps) {
    const Color c = fb.get_pixel(x, y);
    CHECK(pixel_near(c, expected, eps));
}

} // namespace

TEST_CASE("GraphHealth: fullscreen background covers all corners and center") {
    constexpr int W = 1536;
    constexpr int H = 1024;

    SceneBuilder s(W, H);
    s.layer("fullscreen_bg", [](LayerBuilder& l) {
        l.rect("bg", {
            .size = {1536.0f, 1024.0f},
            .color = Color{0.1f, 0.2f, 0.8f, 1.0f},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    Scene scene = s.build();

    auto renderer = test::make_renderer();
    cache::NodeCache node_cache;
    Camera camera;

    auto fb = render_graph_scene(renderer, node_cache, scene, camera, W, H);
    REQUIRE(fb != nullptr);

    auto check_bg = [&](int x, int y) {
        const Color c = fb->get_pixel(x, y);
        CHECK(c.a > 0.9f);
        CHECK(c.b > 0.6f);
        CHECK(c.b > c.r);
    };

    check_bg(10, 10);
    check_bg(W - 10, 10);
    check_bg(10, H - 10);
    check_bg(W - 10, H - 10);
    check_bg(W / 2, H / 2);
}

TEST_CASE("GraphHealth: layer order is preserved") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    s.layer("red_bottom", [](LayerBuilder& l) {
        l.rect("red", {
            .size = {300.0f, 300.0f},
            .color = Color{1, 0, 0, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    s.layer("green_top", [](LayerBuilder& l) {
        l.rect("green", {
            .size = {160.0f, 160.0f},
            .color = Color{0, 1, 0, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    Scene scene = s.build();

    auto renderer = test::make_renderer();
    cache::NodeCache node_cache;
    Camera camera;

    auto fb = render_graph_scene(renderer, node_cache, scene, camera, W, H);
    REQUIRE(fb != nullptr);

    const Color center = fb->get_pixel(W / 2, H / 2);
    CHECK(center.g > 0.8f);
    CHECK(center.r < 0.2f);
}

TEST_CASE("GraphHealth: custom layer translation is applied exactly once") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    s.layer("translated", [](LayerBuilder& l) {
        l.position({100.0f, 50.0f, 0.0f});
        l.rect("box", {
            .size = {80.0f, 80.0f},
            .color = Color{0, 1, 0, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    Scene scene = s.build();

    auto renderer = test::make_renderer();
    cache::NodeCache node_cache;
    Camera camera;

    auto fb = render_graph_scene(renderer, node_cache, scene, camera, W, H);
    REQUIRE(fb != nullptr);

    const Color translated = fb->get_pixel(W / 2 + 100, H / 2 + 50);
    const Color old_center  = fb->get_pixel(W / 2, H / 2);

    CHECK(translated.g > 0.8f);
    CHECK(old_center.g < 0.2f);
}

TEST_CASE("GraphHealth: custom layer scale is applied correctly") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    s.layer("scaled", [](LayerBuilder& l) {
        l.scale({2.0f, 2.0f, 1.0f});
        l.rect("box", {
            .size = {50.0f, 50.0f},
            .color = Color{1, 1, 0, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    Scene scene = s.build();

    auto renderer = test::make_renderer();
    cache::NodeCache node_cache;
    Camera camera;

    auto fb = render_graph_scene(renderer, node_cache, scene, camera, W, H);
    REQUIRE(fb != nullptr);

    const Color center = fb->get_pixel(W / 2, H / 2);
    const Color inside_scaled = fb->get_pixel(W / 2 + 45, H / 2);
    const Color outside_scaled = fb->get_pixel(W / 2 + 80, H / 2);

    CHECK(center.a > 0.9f);
    CHECK(inside_scaled.a > 0.9f);
    CHECK(outside_scaled.r < 0.2f);
}

TEST_CASE("GraphHealth: mask remains aligned with transformed layer") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    s.layer("masked", [](LayerBuilder& l) {
        l.position({80.0f, 0.0f, 0.0f});
        l.mask_rect({
            .size = {100.0f, 100.0f},
            .pos = {0.0f, 0.0f, 0.0f}
        });
        l.rect("big", {
            .size = {240.0f, 240.0f},
            .color = Color{1, 0, 1, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    Scene scene = s.build();

    auto renderer = test::make_renderer();
    cache::NodeCache node_cache;
    Camera camera;

    auto fb = render_graph_scene(renderer, node_cache, scene, camera, W, H);
    REQUIRE(fb != nullptr);

    const Color inside  = fb->get_pixel(W / 2 + 80, H / 2);
    const Color outside = fb->get_pixel(W / 2 + 180, H / 2);

    CHECK(inside.r > 0.7f);
    CHECK(inside.b > 0.7f);
    CHECK(outside.a < 0.2f);
}

TEST_CASE("GraphHealth: glow expands visible pixels outside source bbox") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    s.layer("glow", [](LayerBuilder& l) {
        l.glow(GlowParams{.radius = 50.0f, .intensity = 1.0f, .color = Color{0, 0.7f, 1, 1}});
        l.rect("box", {
            .size = {100.0f, 100.0f},
            .color = Color{1, 1, 1, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    Scene scene = s.build();

    auto renderer = test::make_renderer();
    cache::NodeCache node_cache;
    Camera camera;

    auto fb = render_graph_scene(renderer, node_cache, scene, camera, W, H);
    REQUIRE(fb != nullptr);

    const Color center = fb->get_pixel(W / 2, H / 2);
    const Color glow_near = fb->get_pixel(W / 2 + 75, H / 2);
    const Color glow_far = fb->get_pixel(W / 2 + 170, H / 2);

    CHECK(center.r > 0.8f);
    CHECK(center.g > 0.8f);
    CHECK(center.b > 0.8f);

    CHECK(glow_near.b > 0.01f);
    CHECK(glow_far.b < glow_near.b);
}

TEST_CASE("GraphHealth: glow falloff is radial and fades with distance") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    s.layer("glow", [](LayerBuilder& l) {
        l.glow(GlowParams{.radius = 60.0f, .intensity = 1.0f, .color = Color{0.15f, 0.65f, 1.0f, 1.0f}});
        l.circle("orb", {
            .radius = 42.0f,
            .color = Color{1, 1, 1, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    Scene scene = s.build();

    auto renderer = test::make_renderer();
    cache::NodeCache node_cache;
    Camera camera;

    auto fb = render_graph_scene(renderer, node_cache, scene, camera, W, H);
    REQUIRE(fb != nullptr);

    const Color near_pixel = fb->get_pixel(W / 2 + 48, H / 2);
    const Color mid = fb->get_pixel(W / 2 + 92, H / 2);
    const Color far_pixel = fb->get_pixel(W / 2 + 156, H / 2);
    const Color diag = fb->get_pixel(W / 2 + 92, H / 2 + 92);

    CHECK(near_pixel.b > mid.b);
    CHECK(mid.b > far_pixel.b);
    CHECK(std::abs(mid.b - diag.b) < 0.08f);
}

TEST_CASE("GraphHealth: screen blend brightens underlying layer") {
    constexpr int W = 256;
    constexpr int H = 256;

    SceneBuilder s(W, H);

    s.layer("base", [](LayerBuilder& l) {
        l.rect("base", {
            .size = {256.0f, 256.0f},
            .color = Color{0.2f, 0.2f, 0.2f, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    s.layer("screen", [](LayerBuilder& l) {
        l.blend(BlendMode::Screen);
        l.rect("light", {
            .size = {120.0f, 120.0f},
            .color = Color{0.5f, 0.5f, 1.0f, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    Scene scene = s.build();

    auto renderer = test::make_renderer();
    cache::NodeCache node_cache;
    Camera camera;

    auto fb = render_graph_scene(renderer, node_cache, scene, camera, W, H);
    REQUIRE(fb != nullptr);

    const Color center = fb->get_pixel(W / 2, H / 2);
    CHECK(center.r > 0.2f);
    CHECK(center.g > 0.2f);
    CHECK(center.b > 0.5f);
}

TEST_CASE("GraphHealth: dirty rects on and off render identically") {
    const int W = 256;
    const int H = 256;

    CompositionSpec spec{
        .name = "GraphHealthDirtyRects",
        .width = W,
        .height = H,
        .duration = 3
    };

    Composition comp(spec, [](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);

        s.layer("bg", [](LayerBuilder& l) {
            l.rect("bg", {
                .size = {256.0f, 256.0f},
                .color = Color{0.08f, 0.09f, 0.11f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        const float x = 40.0f + static_cast<float>(ctx.frame) * 20.0f;
        s.layer("moving", [&](LayerBuilder& l) {
            l.position({x - 40.0f, 0.0f, 0.0f});
            l.circle("ball", {
                .radius = 20.0f,
                .color = Color{1, 0.3f, 0.1f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });

    auto baseline = test::make_renderer();
    RenderSettings baseline_settings;
    baseline_settings.use_modular_graph = true;
    baseline_settings.dirty.enabled = false;
    baseline.set_settings(baseline_settings);

    auto dirty = test::make_renderer();
    RenderSettings dirty_settings;
    dirty_settings.use_modular_graph = true;
    dirty_settings.dirty.enabled = true;
    dirty.set_settings(dirty_settings);

    auto fb0_base = baseline.render(comp, 0);
    auto fb0_dirty = dirty.render(comp, 0);
    REQUIRE(fb0_base != nullptr);
    REQUIRE(fb0_dirty != nullptr);

    auto fb1_base = baseline.render(comp, 1);
    auto fb1_dirty = dirty.render(comp, 1);
    REQUIRE(fb1_base != nullptr);
    REQUIRE(fb1_dirty != nullptr);

    for (int y = 0; y < H; y += 8) {
        for (int x = 0; x < W; x += 8) {
            CHECK(pixel_near(fb1_base->get_pixel(x, y), fb1_dirty->get_pixel(x, y), 0.02f));
        }
    }
}

TEST_CASE("GraphHealth: same frame renders deterministically") {
    constexpr int W = 512;
    constexpr int H = 512;

    auto make_scene = [W, H] {
        SceneBuilder s(W, H);

        s.layer("bg", [](LayerBuilder& l) {
            l.rect("bg", {
                .size = {512.0f, 512.0f},
                .color = Color{0.04f, 0.05f, 0.08f, 1},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        s.layer("glow", [](LayerBuilder& l) {
            l.glow(GlowParams{.radius = 30.0f, .intensity = 0.8f, .color = Color{0, 0.8f, 1, 1}});
            l.circle("orb", {
                .radius = 80.0f,
                .color = Color{1, 1, 1, 1},
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    };

    Scene scene_a = make_scene();
    Scene scene_b = make_scene();

    auto renderer_a = test::make_renderer();
    cache::NodeCache node_cache_a;
    Camera camera;

    auto fb_a = render_graph_scene(renderer_a, node_cache_a, scene_a, camera, W, H);
    REQUIRE(fb_a != nullptr);

    auto renderer_b = test::make_renderer();
    cache::NodeCache node_cache_b;
    auto fb_b = render_graph_scene(renderer_b, node_cache_b, scene_b, camera, W, H);
    REQUIRE(fb_b != nullptr);

    for (int y = 0; y < H; y += 8) {
        for (int x = 0; x < W; x += 8) {
            CHECK(pixel_near(fb_a->get_pixel(x, y), fb_b->get_pixel(x, y), 0.001f));
        }
    }
}

TEST_CASE("GraphHealth: graph output matches direct renderer for layered scene") {
    constexpr int W = 512;
    constexpr int H = 512;

    SceneBuilder s(W, H);

    s.layer("bg", [](LayerBuilder& l) {
        l.rect("bg", {
            .size = {512.0f, 512.0f},
            .color = Color{0.05f, 0.05f, 0.1f, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    s.layer("box", [](LayerBuilder& l) {
        l.position({60.0f, 30.0f, 0.0f});
        l.rect("box", {
            .size = {120.0f, 80.0f},
            .color = Color{1, 0.6f, 0.1f, 1},
            .pos = {0.0f, 0.0f, 0.0f}
        });
    });

    Scene scene = s.build();

    auto direct_renderer = test::make_renderer();
    Camera camera;
    auto fb_direct = direct_renderer.render_scene(scene, camera, W, H, 30.0f);

    auto graph_renderer = test::make_renderer();
    cache::NodeCache node_cache;
    auto fb_graph = render_graph_scene(graph_renderer, node_cache, scene, camera, W, H);

    REQUIRE(fb_direct != nullptr);
    REQUIRE(fb_graph != nullptr);

    for (int y = 0; y < H; y += 16) {
        for (int x = 0; x < W; x += 16) {
            CHECK(pixel_near(fb_direct->get_pixel(x, y), fb_graph->get_pixel(x, y), 0.03f));
        }
    }
}
