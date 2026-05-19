#include <doctest/doctest.h>

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/trace.hpp>
#include <chronon3d/core/counters.hpp>

using namespace chronon3d;
using namespace chronon3d::cache;

TEST_CASE("FramebufferPool::acquire returns framebuffer with requested size") {
    FramebufferPool pool(1024 * 1024);

    auto fb = pool.acquire(320, 240);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 320);
    CHECK(fb->height() == 240);
}

TEST_CASE("FramebufferPool::acquired framebuffer is cleared") {
    FramebufferPool pool(1024 * 1024);

    auto fb = pool.acquire(64, 64);
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
    FramebufferPool pool(1024 * 1024);

    auto fb1 = pool.acquire(100, 200);
    auto* fb1_ptr = fb1.get();
    CHECK(pool.available_count() == 0);

    pool.release(std::move(fb1));
    CHECK(pool.available_count() == 1);
    CHECK(pool.current_bytes() > 0);

    auto fb2 = pool.acquire(100, 200);
    // Should reuse the same framebuffer
    CHECK(fb2.get() == fb1_ptr);
    CHECK(pool.available_count() == 0);
}

TEST_CASE("FramebufferPool different sizes go into different buckets") {
    FramebufferPool pool(1024 * 1024);

    auto fb_small = pool.acquire(10, 10);
    auto fb_large = pool.acquire(100, 100);
    CHECK(fb_small.get() != fb_large.get());

    pool.release(std::move(fb_small));
    pool.release(std::move(fb_large));
    CHECK(pool.available_count() == 2);

    // Acquiring the small size should return the small one
    auto fb_reused = pool.acquire(10, 10);
    CHECK(fb_reused->width() == 10);
    CHECK(fb_reused->height() == 10);
    CHECK(pool.available_count() == 1);
}

TEST_CASE("FramebufferPool does not exceed max_bytes") {
    // A 64x64 framebuffer = 64*64*16 = 65536 bytes
    // Set max to ~130KB, which fits 2 framebuffers
    FramebufferPool pool(65536 * 2 + 1);

    auto fb1 = pool.acquire(64, 64);
    auto fb2 = pool.acquire(64, 64);
    auto fb3 = pool.acquire(64, 64);

    pool.release(std::move(fb1));
    pool.release(std::move(fb2));
    // After two releases, ~131KB used (within limit)
    CHECK(pool.available_count() == 2);

    // Third release should be dropped because 3*64K > 130K
    pool.release(std::move(fb3));
    CHECK(pool.available_count() == 2);
}

TEST_CASE("FramebufferPool::clear releases all") {
    FramebufferPool pool(1024 * 1024);

    auto fb1 = pool.acquire(32, 32);
    auto fb2 = pool.acquire(32, 32);
    pool.release(std::move(fb1));
    pool.release(std::move(fb2));
    CHECK(pool.available_count() == 2);

    pool.clear();
    CHECK(pool.available_count() == 0);
    CHECK(pool.current_bytes() == 0);
}

TEST_CASE("PooledFramebuffer RAII handle releases on destruction") {
    FramebufferPool pool(1024 * 1024);

    {
        auto fb = pool.acquire(50, 50);
        PooledFramebuffer guarded(std::move(fb), &pool);
        CHECK(guarded.valid());
        CHECK(guarded->width() == 50);
        CHECK(pool.available_count() == 0);
    }
    // After guard destruction, fb should be back in pool
    CHECK(pool.available_count() == 1);
}

TEST_CASE("PooledFramebuffer move transfers ownership") {
    FramebufferPool pool(1024 * 1024);

    auto fb = pool.acquire(16, 16);
    PooledFramebuffer g1(std::move(fb), &pool);
    CHECK(g1.valid());

    PooledFramebuffer g2(std::move(g1));
    CHECK(g2.valid());
    CHECK(!g1.valid());
    CHECK(pool.available_count() == 0);

    // g2 goes out of scope next, releasing to pool
}

TEST_CASE("PooledFramebuffer detach releases from pool management") {
    FramebufferPool pool(1024 * 1024);

    auto fb = pool.acquire(16, 16);
    auto* raw = fb.get();
    PooledFramebuffer guarded(std::move(fb), &pool);

    auto detached = guarded.detach();
    CHECK(!guarded.valid());
    CHECK(detached.get() == raw);
    CHECK(pool.available_count() == 0);

    // Explicitly release the detached framebuffer
    pool.release(std::move(detached));
    CHECK(pool.available_count() == 1);
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
