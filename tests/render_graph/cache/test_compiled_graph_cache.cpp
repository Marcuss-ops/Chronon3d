#include <doctest/doctest.h>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>

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
    CompiledFrameGraph compiled(std::move(graph));
    cache.store(std::move(compiled), 1920, 1080);

    CHECK(cache.has(1920, 1080));
    CHECK_FALSE(cache.has(1280, 720));
}

TEST_CASE("try_take consumes the cached graph") {
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled(std::move(graph));
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
    CompiledFrameGraph compiled(std::move(graph));
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
    CompiledFrameGraph compiled(std::move(graph));
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
    // ctx.resources.compiled_graph_cache defaults to nullptr.

    // When cache is null, has() should not be called.
    // We verify that can_reuse is false when cache is null by construction:
    CHECK(ctx.resources.compiled_graph_cache == nullptr);
}

TEST_CASE("cache present but dimension mismatch → build fresh") {
    RenderGraphContext ctx;
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled(std::move(graph));
    cache.store(std::move(compiled), 1920, 1080);

    ctx.resources.compiled_graph_cache = &cache;

    // Cache has 1920x1080 but we query 1280x720.
    CHECK_FALSE(cache.has(1280, 720));
    CHECK(cache.has(1920, 1080)); // original still present
}

TEST_CASE("cache compatible → reuse available") {
    RenderGraphContext ctx;
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled(std::move(graph));
    cache.store(std::move(compiled), 3840, 2160);

    ctx.resources.compiled_graph_cache = &cache;

    CHECK(cache.has(3840, 2160));
}

TEST_CASE("try_take consumes the value (coordinator path)") {
    RenderGraphContext ctx;
    CompiledGraphCache cache;

    RenderGraph graph;
    CompiledFrameGraph compiled(std::move(graph));
    cache.store(std::move(compiled), 1920, 1080);

    ctx.resources.compiled_graph_cache = &cache;

    REQUIRE(cache.has(1920, 1080));

    auto taken = cache.try_take(1920, 1080);
    REQUIRE(taken.has_value());

    // Consumed — has() should return false now.
    CHECK_FALSE(cache.has(1920, 1080));
}
