#include <doctest/doctest.h>

#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/color.hpp>

#include "src/render_graph/executor/node_executor.hpp"
#include "src/render_graph/executor/cache_evaluator.hpp"
#include <chronon3d/render_graph/nodes/clear_node.hpp>
#include <chronon3d/core/profiling/counters.hpp>

using namespace chronon3d;
using namespace chronon3d::graph;

TEST_CASE("ClearNode output is frame-variant because compositing mutates it") {
    const ClearNode node;
    CHECK(node.cache_policy().frame_dependent());
    CHECK(cache_frame_for_policy(node.cache_policy(), Frame{0}) == Frame{0});
    CHECK(cache_frame_for_policy(node.cache_policy(), Frame{30}) == Frame{30});
}

TEST_CASE("cache evaluation production path retains sub-millisecond timing") {
    RenderCounters counters;
    RenderGraphContext context;
    context.node_exec.counters = &counters;
    ClearNode node;
    for (int i = 0; i < 1000; ++i)
        (void)evaluate_cache(node, context, 0, false, false, 0, false);
    CHECK(counters.cache_eval_wall_ns.load() > 0);
    CHECK(counters.cache_eval_wall_ms.load() == counters.cache_eval_wall_ns.load() / 1'000'000);
    CHECK(counters.cache_eval_wall_us.load() == counters.cache_eval_wall_ns.load() / 1000);
    context.node_exec.counters = nullptr;
    const auto previous = counters.cache_eval_wall_ns.load();
    (void)evaluate_cache(node, context, 0, false, false, 0, false);
    CHECK(counters.cache_eval_wall_ns.load() == previous);
}

TEST_CASE("NodeCache lifetime policy rejects arena-backed framebuffer storage") {
    FramebufferArena arena(4096);
    auto* pixels = static_cast<Color*>(arena.allocate(16 * 16 * sizeof(Color)));
    REQUIRE(pixels != nullptr);

    Framebuffer framebuffer(16, 16, pixels);
    PoolFbDeleter arena_deleter(DeleteFramebuffer{});

    CHECK(framebuffer.is_arena_allocated());
    CHECK_FALSE(node_output_is_persistently_cacheable(framebuffer, arena_deleter));
}

TEST_CASE("NodeCache lifetime policy accepts independently owned framebuffer storage") {
    Framebuffer framebuffer(16, 16);
    PoolFbDeleter pool_deleter(DeleteFramebuffer{});

    CHECK_FALSE(framebuffer.is_arena_allocated());
    CHECK(node_output_is_persistently_cacheable(framebuffer, pool_deleter));
}

TEST_CASE("NodeCache lifetime policy rejects renderer-owned and native-only outputs") {
    Framebuffer framebuffer(16, 16);
    PoolFbDeleter renderer_deleter(RendererOwned{});
    CHECK_FALSE(node_output_is_persistently_cacheable(framebuffer, renderer_deleter));

    framebuffer.set_surface_handle(1);
    PoolFbDeleter owned_deleter(DeleteFramebuffer{});
    CHECK_FALSE(node_output_is_persistently_cacheable(framebuffer, owned_deleter));
}
