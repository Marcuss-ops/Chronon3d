#include <doctest/doctest.h>

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/trace.hpp>
#include <chronon3d/core/counters.hpp>

using namespace chronon3d;
using namespace chronon3d::cache;

TEST_CASE("FramebufferPool::acquire returns framebuffer with requested size") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024);

    auto fb = pool->acquire(320, 240);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 320);
    CHECK(fb->height() == 240);
}

TEST_CASE("FramebufferPool::acquired framebuffer is cleared") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024);

    auto fb = pool->acquire(64, 64);
    // Check that all pixels are transparent (default clear color)
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            Color c = fb->get_pixel(x, y);
            CHECK(c.r == 0.0f);
            CHECK(c.g == 0.0f);
            CHECK(c.b == 0.0f);
            CHECK(c.a == 0.0f);
        }
    }
}

TEST_CASE("FramebufferPool::release then acquire reuses same size") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024);

    auto fb1 = pool->acquire(100, 200);
    auto* fb1_ptr = fb1.get();
    CHECK(pool->available_count() == 0);

    fb1.reset();
    CHECK(pool->available_count() == 1);
    CHECK(pool->current_bytes() > 0);

    auto fb2 = pool->acquire(100, 200);
    // Should reuse the same framebuffer
    CHECK(fb2.get() == fb1_ptr);
    CHECK(pool->available_count() == 0);
}

TEST_CASE("FramebufferPool different sizes go into different buckets") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024);

    auto fb_small = pool->acquire(10, 10);
    auto fb_large = pool->acquire(100, 100);
    auto* small_ptr = fb_small.get();
    auto* large_ptr = fb_large.get();
    CHECK(small_ptr != large_ptr);

    fb_small.reset();
    fb_large.reset();
    CHECK(pool->available_count() == 2);

    // Acquiring the small size should return the small one
    auto fb_reused = pool->acquire(10, 10);
    CHECK(fb_reused->width() == 10);
    CHECK(fb_reused->height() == 10);
    CHECK(fb_reused.get() == small_ptr);
    CHECK(pool->available_count() == 1);
}

TEST_CASE("FramebufferPool does not exceed max_bytes") {
    // A 64x64 framebuffer = 64*64*16 = 65536 bytes
    // Set max to ~130KB, which fits 2 framebuffers
    auto pool = std::make_shared<FramebufferPool>(65536 * 2 + 1);

    auto fb1 = pool->acquire(64, 64);
    auto fb2 = pool->acquire(64, 64);
    auto fb3 = pool->acquire(64, 64);

    fb1.reset();
    fb2.reset();
    // After two releases, ~131KB used (within limit)
    CHECK(pool->available_count() == 2);

    // Third release should be dropped because 3*64K > 130K
    fb3.reset();
    CHECK(pool->available_count() == 2);
}

TEST_CASE("FramebufferPool::clear releases all") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024);

    auto fb1 = pool->acquire(32, 32);
    auto fb2 = pool->acquire(32, 32);
    fb1.reset();
    fb2.reset();
    CHECK(pool->available_count() == 2);

    pool->clear();
    CHECK(pool->available_count() == 0);
    CHECK(pool->current_bytes() == 0);
}

TEST_CASE("framebuffer_pool_reuses_after_release") {
    RenderCounters counters;
    profiling::g_current_counters = &counters;

    auto pool = std::make_shared<FramebufferPool>();
    REQUIRE(counters.framebuffer_reuses.load() == 0);

    {
        auto a = pool->acquire_pooled(1920, 1080, pool);
    }
    // After release, no reuse yet
    REQUIRE(counters.framebuffer_reuses.load() == 0);

    {
        auto b = pool->acquire_pooled(1920, 1080, pool);
    }
    // Second acquire should have reused the released framebuffer
    REQUIRE(counters.framebuffer_reuses.load() >= 1);

    profiling::g_current_counters = nullptr;
}
