#include <doctest/doctest.h>

#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/color.hpp>

#include "src/render_graph/executor/node_executor.hpp"

using namespace chronon3d;
using namespace chronon3d::graph;

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
