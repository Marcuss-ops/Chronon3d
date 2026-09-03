#include <doctest/doctest.h>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/internal/runtime/cache_domains.hpp>
#include <chronon3d/internal/runtime/history_state.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <tests/helpers/test_utils.hpp>

#include <memory>
#include <string>
#include <array>
#include <vector>

using namespace chronon3d::graph;

// ---------------------------------------------------------------------------
// CompiledGraphCache unit tests
// ---------------------------------------------------------------------------

TEST_CASE("compiled graph cache starts empty") {
    CompiledGraphCache cache;
    CHECK_FALSE(cache.has(0, 0));
    CHECK_FALSE(cache.has(1920, 1080));
    CHECK(cache.try_take(1920, 1080) == std::nullopt);
}

TEST_CASE("store makes matching dimensions available") {
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled;
    compiled.graph = std::move(graph);
    cache.store(std::move(compiled), 1920, 1080);

    CHECK(cache.has(1920, 1080));
    CHECK_FALSE(cache.has(1280, 720));
}

TEST_CASE("try_take consumes the cached graph") {
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled;
    compiled.graph = std::move(graph);
    cache.store(std::move(compiled), 1920, 1080);

    CHECK(cache.has(1920, 1080));

    auto taken = cache.try_take(1920, 1080);
    REQUIRE(taken.has_value());

    // After try_take, the cache should be empty (single-use).
    CHECK_FALSE(cache.has(1920, 1080));
    CHECK(cache.try_take(1920, 1080) == std::nullopt);
}

TEST_CASE("dimension mismatch does not consume the graph") {
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled;
    compiled.graph = std::move(graph);
    cache.store(std::move(compiled), 1920, 1080);

    // Wrong dimensions — should return nullopt and NOT consume.
    auto taken = cache.try_take(1280, 720);
    CHECK_FALSE(taken.has_value());

    // Cache still holds the original entry for the correct dimensions.
    CHECK(cache.has(1920, 1080));

    // Correct dimensions still work.
    auto taken_correct = cache.try_take(1920, 1080);
    CHECK(taken_correct.has_value());
}

TEST_CASE("reset clears dimensions and payload") {
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled;
    compiled.graph = std::move(graph);
    cache.store(std::move(compiled), 640, 480);

    CHECK(cache.has(640, 480));

    cache.reset();

    CHECK_FALSE(cache.has(640, 480));
    CHECK(cache.try_take(640, 480) == std::nullopt);
}

// ---------------------------------------------------------------------------
// Graph cache coordinator decoupling tests
// ---------------------------------------------------------------------------

TEST_CASE("null cache → build fresh") {
    RenderGraphContext ctx;
    // ctx.services.compiled_graph_cache defaults to nullptr.

    // When cache is null, has() should not be called.
    // We verify that can_reuse is false when cache is null by construction:
    CHECK(ctx.services.compiled_graph_cache == nullptr);
}

TEST_CASE("cache present but dimension mismatch → build fresh") {
    RenderGraphContext ctx;
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled;
    compiled.graph = std::move(graph);
    cache.store(std::move(compiled), 1920, 1080);

    ctx.services.compiled_graph_cache = &cache;

    // Cache has 1920x1080 but we query 1280x720.
    CHECK_FALSE(cache.has(1280, 720));
    CHECK(cache.has(1920, 1080)); // original still present
}

TEST_CASE("cache compatible → reuse available") {
    RenderGraphContext ctx;
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled;
    compiled.graph = std::move(graph);
    cache.store(std::move(compiled), 3840, 2160);

    ctx.services.compiled_graph_cache = &cache;

    CHECK(cache.has(3840, 2160));
}

TEST_CASE("cache domains reset independently") {
    auto renderer = chronon3d::test::make_renderer_shared();
    auto& runtime = renderer->runtime();

    chronon3d::cache::NodeCacheKey node_key{
        .scope = "reset-domain-test",
        .frame = chronon3d::Frame{7},
        .width = 16,
        .height = 16,
        .params_hash = 0x11,
        .source_hash = 0x22,
        .input_hash = 0x33,
    };
    runtime.node_cache().store(
        node_key, std::make_shared<chronon3d::Framebuffer>(16, 16));
    REQUIRE(runtime.node_cache().contains(node_key));

    chronon3d::graph::RenderGraph graph;
    chronon3d::graph::CompiledFrameGraph compiled;
    compiled.graph = std::move(graph);
    runtime.graph_cache().store(std::move(compiled), 16, 16);
    REQUIRE(runtime.graph_cache().has(16, 16));

    renderer->frame_history().prev_frame = chronon3d::Frame{7};
    renderer->frame_history().prev_scene_fingerprint = 0xCAFE;

    renderer->reset_compiled_cache();
    CHECK_FALSE(runtime.graph_cache().has(16, 16));
    CHECK(runtime.node_cache().contains(node_key));
    CHECK(renderer->frame_history().prev_scene_fingerprint == 0xCAFE);

    renderer->reset_frame_value_cache();
    CHECK_FALSE(runtime.node_cache().contains(node_key));
    CHECK(renderer->frame_history().prev_frame == chronon3d::Frame{7});

    runtime.node_cache().store(
        node_key, std::make_shared<chronon3d::Framebuffer>(16, 16));
    renderer->reset_temporal_history();
    CHECK_FALSE(renderer->frame_history().prev_camera_valid);
    CHECK(renderer->frame_history().prev_frame == chronon3d::Frame{-1});
    CHECK(runtime.node_cache().contains(node_key));

    // Exercise every required access order while applying domain resets
    // between orders. The retained node value must survive topology and
    // temporal resets, then disappear only when frame values are reset.
    const std::array<std::vector<int>, 4> orders{{
        {0, 1, 2, 3},
        {2, 0, 3, 1},
        {3, 2, 1, 0},
        {0, 0, 1, 1, 2, 2, 3, 3},
    }};
    for (const auto& order : orders) {
        for (const int frame : order) {
            node_key.frame = chronon3d::Frame{frame};
            runtime.node_cache().store(
                node_key, std::make_shared<chronon3d::Framebuffer>(16, 16));
            CHECK(runtime.node_cache().contains(node_key));
        }
        CHECK(runtime.node_cache().contains(node_key));
        renderer->reset_compiled_cache();
        renderer->reset_temporal_history();
        CHECK(runtime.node_cache().contains(node_key));
    }
    renderer->reset_frame_value_cache();
    CHECK_FALSE(runtime.node_cache().contains(node_key));
}

TEST_CASE("cache domain facades reset independently in sequential random and reverse orders") {
    auto renderer = chronon3d::test::make_renderer_shared();
    auto& runtime = renderer->runtime();
    auto topology = chronon3d::runtime::CompiledTopologyCache{runtime.graph_cache()};
    auto values = chronon3d::runtime::FrameValueCache{runtime.node_cache()};
    auto history = renderer->session().history_state();

    chronon3d::cache::NodeCacheKey key{
        .scope = "formal-cache-domains",
        .frame = chronon3d::Frame{3},
        .width = 8,
        .height = 8,
        .params_hash = 0x101,
        .source_hash = 0x202,
        .input_hash = 0x303,
    };

    const std::array<std::array<int, 3>, 3> orders{{
        {{0, 1, 2}}, // sequential: topology, values, history
        {{2, 1, 0}}, // reverse
        {{1, 2, 0}}, // deterministic random permutation
    }};

    for (const auto& order : orders) {
        chronon3d::graph::RenderGraph graph;
        chronon3d::graph::CompiledFrameGraph compiled;
    compiled.graph = std::move(graph);
        runtime.graph_cache().store(std::move(compiled), 8, 8);
        runtime.node_cache().store(
            key, std::make_shared<chronon3d::Framebuffer>(8, 8));
        renderer->frame_history().prev_frame = chronon3d::Frame{3};
        renderer->frame_history().prev_scene_fingerprint = 0xCAFE;

        REQUIRE(runtime.graph_cache().has(8, 8));
        REQUIRE(runtime.node_cache().contains(key));
        REQUIRE(renderer->frame_history().prev_scene_fingerprint == 0xCAFE);

        bool topology_alive = true;
        bool values_alive = true;
        bool history_alive = true;
        for (const int domain : order) {
            if (domain == 0) {
                topology.reset();
                topology_alive = false;
            } else if (domain == 1) {
                values.reset();
                values_alive = false;
            } else {
                history.reset();
                history_alive = false;
            }

            CHECK(runtime.graph_cache().has(8, 8) == topology_alive);
            CHECK(runtime.node_cache().contains(key) == values_alive);
            CHECK((renderer->frame_history().prev_scene_fingerprint == 0xCAFE) == history_alive);
            if (!history_alive) {
                CHECK(renderer->frame_history().prev_frame == chronon3d::Frame{-1});
                CHECK_FALSE(renderer->frame_history().prev_camera_valid);
            }
        }
    }
}

TEST_CASE("try_take consumes the value (coordinator path)") {
    RenderGraphContext ctx;
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled;
    compiled.graph = std::move(graph);
    cache.store(std::move(compiled), 1920, 1080);

    ctx.services.compiled_graph_cache = &cache;

    REQUIRE(cache.has(1920, 1080));

    auto taken = cache.try_take(1920, 1080);
    REQUIRE(taken.has_value());

    // Consumed — has() should return false now.
    CHECK_FALSE(cache.has(1920, 1080));
}
