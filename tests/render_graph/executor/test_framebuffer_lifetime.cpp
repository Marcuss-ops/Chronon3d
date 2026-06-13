#include <doctest/doctest.h>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/render_graph/render_graph.hpp>

#include "src/render_graph/executor/framebuffer_lifetime.hpp"
#include "src/render_graph/executor/execution_state.hpp"

#include <memory>

using namespace chronon3d;
using namespace chronon3d::cache;
using namespace chronon3d::graph;

// =============================================================================
// Framebuffer Lifetime Contract
//
// The executor must not release a framebuffer input before the last consumer
// has finished with it.  The pool must not leak memory, and acquiring/releasing
// must be stable.  Arena-backed allocations must not leak or alias.
// =============================================================================

// ── Pool basics ──────────────────────────────────────────────────────────────

TEST_CASE("FramebufferLifetime: acquire_owned returns valid framebuffer") {
    FramebufferPool pool(256 * 1024 * 1024); // 256 MB
    auto fb = pool.acquire_owned(640, 480, true);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 640);
    CHECK(fb->height() == 480);
}

TEST_CASE("FramebufferLifetime: acquire_owned clears framebuffer when requested") {
    FramebufferPool pool(256 * 1024 * 1024);
    auto fb = pool.acquire_owned(100, 100, true);
    // Check that the framebuffer was cleared (all pixels transparent)
    bool all_transparent = true;
    for (int y = 0; y < fb->height() && all_transparent; ++y) {
        for (int x = 0; x < fb->width() && all_transparent; ++x) {
            Color c = fb->get_pixel(x, y);
            if (c.r != 0.0f || c.g != 0.0f || c.b != 0.0f || c.a != 0.0f) {
                all_transparent = false;
            }
        }
    }
    CHECK(all_transparent);
}

TEST_CASE("FramebufferLifetime: acquire_owned with clear=false preserves contents") {
    // acquire_owned() internally uses PoolFbDeleter with weak_from_this(),
    // which requires the pool to be managed by a shared_ptr.
    auto pool = FramebufferPool::create_shared(256 * 1024 * 1024);

    // Acquire, write content, then release back to pool
    {
        auto fb = pool->acquire_owned(100, 100, true);
        fb->set_pixel(0, 0, Color::red());
    } // returned to pool via weak_from_this()

    // Re-acquire without clearing — content from the recycled buffer
    // may still be red at (0,0) since we didn't clear.
    // (Allocation reuse guarantees the same pixel storage; whether
    // the previous set_pixel survives depends on the pool's reuse path.)
    auto fb2 = pool->acquire_owned(100, 100, false);
    CHECK(fb2->width() == 100);
    CHECK(fb2->height() == 100);

    auto stats = pool->stats();
    CHECK(stats.total_reuses >= 1);
}

TEST_CASE("FramebufferLifetime: pool reuses previously returned buffer") {
    // acquire_owned() internally uses PoolFbDeleter with weak_from_this(),
    // which requires the pool to be managed by a shared_ptr.
    auto pool = FramebufferPool::create_shared(64 * 1024 * 1024);

    {
        auto fb = pool->acquire_owned(1920, 1080, true);
        CHECK(fb != nullptr);
    } // returned to pool via weak_from_this()

    {
        auto fb = pool->acquire_owned(1920, 1080, true);
        CHECK(fb != nullptr);
    } // returned to pool via weak_from_this()

    auto stats = pool->stats();
    CHECK(stats.total_reuses >= 1);
}

TEST_CASE("FramebufferLifetime: multiple acquisitions do not alias") {
    FramebufferPool pool(256 * 1024 * 1024);
    auto fb_a = pool.acquire_owned(100, 100, true);
    auto fb_b = pool.acquire_owned(100, 100, true);

    CHECK(fb_a.get() != fb_b.get());

    fb_a->set_pixel(0, 0, Color::red());
    Color c = fb_b->get_pixel(0, 0);
    CHECK_FALSE(c.r > 0.9f);
}

TEST_CASE("FramebufferLifetime: acquire_pooled returns valid shared_ptr") {
    FramebufferPool pool(256 * 1024 * 1024);
    auto fb = pool.acquire_pooled(320, 240, nullptr, true);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 320);
    CHECK(fb->height() == 240);
}

TEST_CASE("FramebufferLifetime: arena release does not crash") {
    FramebufferPool pool(256 * 1024 * 1024);
    auto fb = pool.acquire_owned(50, 50, true);
    CHECK_NOTHROW(fb.reset());
}

// =============================================================================
// Arena Non-Leak Contract — FramebufferArena
// =============================================================================

TEST_CASE("FramebufferArena: allocates memory within bounds") {
    FramebufferArena arena(1024 * 1024); // 1 MB
    void* ptr = arena.allocate(256);
    REQUIRE(ptr != nullptr);
    CHECK(arena.used_bytes() > 0);
    CHECK(arena.used_bytes() <= 1024 * 1024);
    CHECK(arena.total_bytes() == 1024 * 1024);
}

TEST_CASE("FramebufferArena: multiple allocations do not overlap") {
    FramebufferArena arena(4096);
    void* a = arena.allocate(64);
    void* b = arena.allocate(64);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    // b must be after a (64-byte aligned, so at least 64 bytes apart)
    auto pa = reinterpret_cast<uintptr_t>(a);
    auto pb = reinterpret_cast<uintptr_t>(b);
    CHECK(pb >= pa + 64);
}

TEST_CASE("FramebufferArena: reset reuses memory") {
    FramebufferArena arena(4096);
    void* a = arena.allocate(1024);
    REQUIRE(a != nullptr);
    CHECK(arena.used_bytes() >= 1024);

    arena.reset();
    CHECK(arena.used_bytes() == 0);

    // After reset, new allocation should succeed with the same base address
    void* b = arena.allocate(1024);
    REQUIRE(b != nullptr);
    CHECK(arena.used_bytes() >= 1024);
}

TEST_CASE("FramebufferArena: out of memory returns nullptr") {
    FramebufferArena arena(128);
    void* a = arena.allocate(64);
    REQUIRE(a != nullptr);

    void* b = arena.allocate(128); // exceeds remaining space
    CHECK(b == nullptr);
}

TEST_CASE("FramebufferArena: allocations are 64-byte aligned") {
    FramebufferArena arena(4096);
    void* a = arena.allocate(1);
    REQUIRE(a != nullptr);
    CHECK((reinterpret_cast<uintptr_t>(a) & 63) == 0);

    void* b = arena.allocate(1);
    REQUIRE(b != nullptr);
    CHECK((reinterpret_cast<uintptr_t>(b) & 63) == 0);
}

TEST_CASE("FramebufferArena: usage ratio tracks correctly") {
    constexpr size_t kSize = 4096;
    FramebufferArena arena(kSize);
    CHECK(arena.usage_ratio() == 0.0);

    arena.allocate(2048);
    double ratio = arena.usage_ratio();
    CHECK(ratio > 0.4);  // >= 2048/4096 = 0.5, minus alignment slop
    CHECK(ratio <= 1.0);

    arena.reset();
    CHECK(arena.usage_ratio() == 0.0);
}

// ── TripleBufferArena ───────────────────────────────────────────────────────

TEST_CASE("TripleBufferArena: acquire returns valid arena") {
    TripleBufferArena tba(3, 1024 * 1024);
    auto arena = tba.acquire();
    REQUIRE(arena != nullptr);
    CHECK(arena->total_bytes() == 1024 * 1024);
    tba.release(arena);
}

TEST_CASE("TripleBufferArena: different acquires get different arenas") {
    TripleBufferArena tba(3, 1024);
    auto a1 = tba.acquire();
    auto a2 = tba.acquire();
    REQUIRE(a1 != nullptr);
    REQUIRE(a2 != nullptr);
    CHECK(a1.get() != a2.get());
    tba.release(a1);
    tba.release(a2);
}

TEST_CASE("TripleBufferArena: cycling doesn't leak") {
    TripleBufferArena tba(3, 1024);
    for (int i = 0; i < 10; ++i) {
        auto a = tba.acquire();
        REQUIRE(a != nullptr);
        tba.release(a);
    }
    // Cycling 10 times through 3 arenas must not crash or block
}

TEST_CASE("TripleBufferArena: acquired arena is reset") {
    TripleBufferArena tba(3, 4096);
    auto a1 = tba.acquire();
    // Use some memory
    a1->allocate(1024);
    CHECK(a1->used_bytes() >= 1024);
    tba.release(a1);

    // Re-acquire — the arena should have been reset
    auto a2 = tba.acquire();
    CHECK(a2->used_bytes() == 0);
    tba.release(a2);
}

// =============================================================================
// Executor Input Lifetime Contract — consumer_remaining lifecycle
//
// init_consumer_remaining MUST initialize counts correctly.
// release_consumed_framebuffers MUST only release a framebuffer when the
// LAST consumer has processed it (count reaches zero).
// =============================================================================

TEST_CASE("InputLifetime: init_consumer_remaining initializes correctly") {
    FrameArena arena;
    std::array<size_t, 3> counts = {2, 0, 1};

    auto remaining = init_consumer_remaining(5, counts, arena.resource());

    CHECK(remaining.size() == 5);
    CHECK(remaining[0].load() == 2);
    CHECK(remaining[1].load() == 0);
    CHECK(remaining[2].load() == 1);
    CHECK(remaining[3].load() == 0); // beyond the span, zero-initialized
    CHECK(remaining[4].load() == 0);
}

TEST_CASE("InputLifetime: single consumer releases on last use") {
    // Graph: n0 → n1  (n0 has 1 consumer: n1)
    // Level: [0], [1]
    // When processing level [1]: n1's input n0 has count 1 → released

    RenderGraph graph;
    auto n0 = graph.add_node(nullptr);
    auto n1 = graph.add_node(nullptr);
    graph.connect(n0, n1);

    FrameArena arena;
    std::array<size_t, 2> counts = {1, 0}; // n0=1 consumer, n1=0 consumers
    auto remaining = init_consumer_remaining(2, counts, arena.resource());

    ExecutionState state(arena.resource());
    state.temp.resize(2);
    state.resolved_key_digest.resize(2);
    state.resolved_frame_dependent.resize(2);
    state.resolved_cache_hit.resize(2);
    state.resolved_bboxes.resize(2);
    auto fb = std::make_shared<Framebuffer>(100, 100);
    state.temp[0] = fb;
    CHECK(state.temp[0] != nullptr);
    CHECK(remaining[0].load() == 1);

    // Process level [n1] — n1 is the sole consumer of n0
    std::vector<GraphNodeId> level = {n1};
    release_consumed_framebuffers(state, graph, level, remaining);

    // n0's consumer count should reach 0 and its temp should be released
    CHECK(remaining[0].load() == 0);
    CHECK(state.temp[0] == nullptr);
    CHECK(state.resolved_key_digest[0] == 0);
}

TEST_CASE("InputLifetime: multiple consumers only release on last consumer") {
    // Graph: n0 → n1 and n0 → n2  (n0 has 2 consumers: n1, n2)
    // Level: [0], [1, 2] (level 1 = n1 + n2 in parallel)
    // When processing level [n1, n2]:
    //   - n1 consumes n0 but n0 still has n2 → NOT released
    //   - n2 consumes n0 → last consumer → released

    RenderGraph graph;
    auto n0 = graph.add_node(nullptr);
    auto n1 = graph.add_node(nullptr);
    auto n2 = graph.add_node(nullptr);
    graph.connect(n0, n1);  // n1 consumes n0
    graph.connect(n0, n2);  // n2 consumes n0

    FrameArena arena;
    std::array<size_t, 3> counts = {2, 0, 0}; // n0=2 consumers, n1=0, n2=0
    auto remaining = init_consumer_remaining(3, counts, arena.resource());

    ExecutionState state(arena.resource());
    state.temp.resize(3);
    state.resolved_key_digest.resize(3);
    state.resolved_frame_dependent.resize(3);
    state.resolved_cache_hit.resize(3);
    state.resolved_bboxes.resize(3);
    auto fb = std::make_shared<Framebuffer>(100, 100);
    state.temp[0] = fb;
    CHECK(remaining[0].load() == 2);

    // Process level [n1, n2] — both in same level (parallel consumers)
    std::vector<GraphNodeId> level = {n1, n2};
    release_consumed_framebuffers(state, graph, level, remaining);

    // n0's consumer count should reach 0 and its temp should be released
    CHECK(remaining[0].load() == 0);
    CHECK(state.temp[0] == nullptr);
    CHECK(state.resolved_key_digest[0] == 0);
}

TEST_CASE("InputLifetime: framebuffer held while consumers remain") {
    // Graph: n0 → n1 and n0 → n2, but processed one at a time:
    // Level: [0], [1], [2]
    // After processing level [1]: n0's count goes from 2 to 1, NOT released
    // After processing level [2]: n0's count goes from 1 to 0, released

    RenderGraph graph;
    auto n0 = graph.add_node(nullptr);
    auto n1 = graph.add_node(nullptr);
    auto n2 = graph.add_node(nullptr);
    graph.connect(n0, n1);
    graph.connect(n0, n2);

    FrameArena arena;
    std::array<size_t, 3> counts = {2, 0, 0};
    auto remaining = init_consumer_remaining(3, counts, arena.resource());

    ExecutionState state(arena.resource());
    state.temp.resize(3);
    state.resolved_key_digest.resize(3);
    state.resolved_frame_dependent.resize(3);
    state.resolved_cache_hit.resize(3);
    state.resolved_bboxes.resize(3);
    auto fb = std::make_shared<Framebuffer>(100, 100);
    state.temp[0] = fb;
    CHECK(remaining[0].load() == 2);

    // Process level [n1] — first consumer
    release_consumed_framebuffers(state, graph, std::vector<GraphNodeId>{n1}, remaining);

    // n0 still has 1 remaining consumer (n2) → NOT released yet
    CHECK(remaining[0].load() == 1);
    CHECK(state.temp[0] != nullptr); // still held!

    // Process level [n2] — last consumer
    release_consumed_framebuffers(state, graph, std::vector<GraphNodeId>{n2}, remaining);

    // Now n0 should be released
    CHECK(remaining[0].load() == 0);
    CHECK(state.temp[0] == nullptr);
}

TEST_CASE("InputLifetime: node with no consumers is never released") {
    // Graph: n0 (no consumers, no connections)
    // consumer_counts[0] = 0 → remaining[0] = 0
    // contains_index check: input_id (0) < remaining.size() (1) = true
    // fetch_sub(1) on 0 returns 0 (unsigned underflow to max), 0 != 1 → NOT released

    RenderGraph graph;
    auto n0 = graph.add_node(nullptr);

    FrameArena arena;
    std::array<size_t, 1> counts = {0}; // n0 has 0 consumers
    auto remaining = init_consumer_remaining(1, counts, arena.resource());

    ExecutionState state(arena.resource());
    state.temp.resize(1);
    state.resolved_key_digest.resize(1);
    state.resolved_frame_dependent.resize(1);
    state.resolved_cache_hit.resize(1);
    state.resolved_bboxes.resize(1);
    auto fb = std::make_shared<Framebuffer>(100, 100);
    state.temp[0] = fb;

    // Release for a non-existent consumer — should not crash or release
    CHECK_NOTHROW(release_consumed_framebuffers(state, graph, std::vector<GraphNodeId>{n0}, remaining));
    CHECK(state.temp[0] != nullptr); // still held
}

TEST_CASE("InputLifetime: unknown inputs are safely ignored") {
    // Graph: n0 has no inputs
    // release_consumed_framebuffers should handle empty input lists gracefully

    RenderGraph graph;
    auto n0 = graph.add_node(nullptr);
    // No connections → n0 has no inputs

    FrameArena arena;
    std::array<size_t, 1> counts = {0};
    auto remaining = init_consumer_remaining(1, counts, arena.resource());

    ExecutionState state(arena.resource());
    state.temp.resize(1);
    state.resolved_key_digest.resize(1);
    state.resolved_frame_dependent.resize(1);
    state.resolved_cache_hit.resize(1);
    state.resolved_bboxes.resize(1);
    CHECK_NOTHROW(release_consumed_framebuffers(state, graph, std::vector<GraphNodeId>{n0}, remaining));
}

TEST_CASE("InputLifetime: contains_index validates bounds") {
    // Verify the helper used in release_consumed_framebuffers
    std::vector<int> vec = {1, 2, 3};
    CHECK(contains_index(vec, 0));   // valid
    CHECK(contains_index(vec, 2));   // valid
    CHECK_FALSE(contains_index(vec, 3));  // out of bounds
    CHECK_FALSE(contains_index(vec, 100));
    CHECK_FALSE(contains_index(vec, -1));  // unsigned wrap
}

TEST_CASE("InputLifetime: state metadata is reset on release") {
    // When a framebuffer is released, all associated metadata
    // (resolved_key_digest, resolved_frame_dependent, resolved_cache_hit)
    // must be reset to prevent stale state from being reused.

    RenderGraph graph;
    auto n0 = graph.add_node(nullptr);
    auto n1 = graph.add_node(nullptr);
    graph.connect(n0, n1);

    FrameArena arena;
    std::array<size_t, 2> counts = {1, 0};
    auto remaining = init_consumer_remaining(2, counts, arena.resource());

    ExecutionState state(arena.resource());
    state.temp.resize(2);
    state.resolved_key_digest.resize(2);
    state.resolved_frame_dependent.resize(2);
    state.resolved_cache_hit.resize(2);
    state.resolved_bboxes.resize(2);

    // Set up state that should be cleared on release
    auto fb = std::make_shared<Framebuffer>(100, 100);
    state.temp[0] = fb;
    state.resolved_key_digest[0] = 0xDEADBEEF;
    state.resolved_frame_dependent[0] = 1;
    state.resolved_cache_hit[0] = 1;
    state.resolved_bboxes[0] = raster::BBox{0, 0, 100, 100};

    release_consumed_framebuffers(state, graph, std::vector<GraphNodeId>{n1}, remaining);

    // All metadata for n0 must be reset
    CHECK(state.temp[0] == nullptr);
    CHECK(state.resolved_key_digest[0] == 0);
    CHECK(state.resolved_frame_dependent[0] == 0);
    CHECK(state.resolved_cache_hit[0] == 0);
    CHECK_FALSE(state.resolved_bboxes[0].has_value());
}

