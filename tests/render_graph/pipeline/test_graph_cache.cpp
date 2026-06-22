#include <doctest/doctest.h>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;

// Helper: render a single frame and return the framebuffer.
// Uses non-consecutive frames and disables dirty rects so that the
// pre-existing fast-paths (resolved_scene_reuse / fast_path_reuse) do
// NOT swallow the frame before the graph cache is exercised.
static std::shared_ptr<Framebuffer> render_frame(
    SoftwareRenderer& renderer,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    Frame frame,
    int w = 100,
    int h = 100
) {
    return render_scene_via_graph(
    renderer.backend(),
        node_cache,
        scene,
        camera,
        w, h,
        frame, 0.0f,
        renderer.render_settings(),
        renderer.composition_registry(),
        renderer.video_decoder()
    );
}

TEST_CASE("GraphCache - cache hit on structurally identical frames") {
    SceneBuilder builder;
    builder.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    // Disable dirty rects so fast_path_reuse does not trigger.
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);

    cache::NodeCache node_cache;
    Camera camera;

    // Frame 0 — cold start, graph cache miss (no prev fingerprint)
    auto fb0 = render_frame(renderer, node_cache, scene, camera, Frame{0});
    REQUIRE(fb0 != nullptr);

    const auto hits_before = renderer.counters()->graph_cache_hits.load();
    const auto misses_before = renderer.counters()->graph_cache_misses.load();

    // Frame 2 (non-consecutive) — structurally identical scene → graph cache hit.
    // Frame 1 would be caught by resolved_scene_reuse, so we skip it.
    auto fb1 = render_frame(renderer, node_cache, scene, camera, Frame{2});
    REQUIRE(fb1 != nullptr);

    CHECK(renderer.counters()->graph_cache_hits.load() == hits_before + 1);
    CHECK(renderer.counters()->graph_cache_misses.load() == misses_before);
}

TEST_CASE("GraphCache - cache miss when dimensions change") {
    SceneBuilder builder;
    builder.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);

    cache::NodeCache node_cache;
    Camera camera;

    // Warm-up frame 0
    render_frame(renderer, node_cache, scene, camera, Frame{0});

    // Frame 2 with same scene → should be a graph cache hit
    const auto hits_before = renderer.counters()->graph_cache_hits.load();
    render_frame(renderer, node_cache, scene, camera, Frame{2});
    CHECK(renderer.counters()->graph_cache_hits.load() == hits_before + 1);

    // Frame 4 with different dimensions → graph topology changes (clip rects)
    auto misses_before = renderer.counters()->graph_cache_misses.load();
    auto fb_diff_size = render_frame(renderer, node_cache, scene, camera, Frame{4}, 200, 200);
    REQUIRE(fb_diff_size != nullptr);
    CHECK(renderer.counters()->graph_cache_misses.load() == misses_before + 1);
}

TEST_CASE("GraphCache - cache miss when layer added") {
    SceneBuilder builder_a;
    builder_a.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene_a = builder_a.build();

    SceneBuilder builder_b;
    builder_b.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    builder_b.rect("b", {.size={30.0f, 30.0f}, .color=Color::blue(), .pos={10.0f, 10.0f, 0.0f}});
    Scene scene_b = builder_b.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);

    cache::NodeCache node_cache;
    Camera camera;

    // Frame 0 with scene_a
    render_frame(renderer, node_cache, scene_a, camera, Frame{0});

    // Frame 2 with scene_a again → hit
    auto hits_before = renderer.counters()->graph_cache_hits.load();
    render_frame(renderer, node_cache, scene_a, camera, Frame{2});
    CHECK(renderer.counters()->graph_cache_hits.load() == hits_before + 1);

    // Frame 4 with scene_b (extra layer) → miss (different static fingerprint)
    auto misses_before = renderer.counters()->graph_cache_misses.load();
    auto fb_b = render_frame(renderer, node_cache, scene_b, camera, Frame{4});
    REQUIRE(fb_b != nullptr);
    CHECK(renderer.counters()->graph_cache_misses.load() == misses_before + 1);
}

TEST_CASE("GraphCache - pixel output matches non-cached path") {
    SceneBuilder builder;
    builder.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene = builder.build();

    // --- Cached path: frame 0 builds, frame 2 reuses cached graph ---
    auto renderer_cached = test::make_renderer();
    RenderSettings settings_c = renderer_cached.render_settings();
    settings_c.dirty.enabled = false;
    renderer_cached.set_settings(settings_c);
    cache::NodeCache node_cache_cached;
    Camera camera;

    render_frame(renderer_cached, node_cache_cached, scene, camera, Frame{0});
    auto fb_cached = render_frame(renderer_cached, node_cache_cached, scene, camera, Frame{2});
    REQUIRE(fb_cached != nullptr);
    CHECK(renderer_cached.counters()->graph_cache_hits.load() >= 1);

    // --- Non-cached path: clear graph cache before every frame ---
    auto renderer_fresh = test::make_renderer();
    RenderSettings settings_f = renderer_fresh.render_settings();
    settings_f.dirty.enabled = false;
    renderer_fresh.set_settings(settings_f);
    cache::NodeCache node_cache_fresh;

    render_frame(renderer_fresh, node_cache_fresh, scene, camera, Frame{0});
    renderer_fresh.clear_caches(); // invalidate graph cache
    auto fb_fresh = render_frame(renderer_fresh, node_cache_fresh, scene, camera, Frame{2});
    REQUIRE(fb_fresh != nullptr);
    CHECK(renderer_fresh.counters()->graph_cache_hits.load() == 0);

    // Compare pixel-for-pixel
    REQUIRE(fb_cached->width() == fb_fresh->width());
    REQUIRE(fb_cached->height() == fb_fresh->height());

    bool matches = true;
    for (int y = 0; y < fb_cached->height(); ++y) {
        for (int x = 0; x < fb_cached->width(); ++x) {
            Color c1 = fb_cached->get_pixel(x, y);
            Color c2 = fb_fresh->get_pixel(x, y);
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
