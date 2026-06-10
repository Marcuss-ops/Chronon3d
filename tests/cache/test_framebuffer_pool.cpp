#include <doctest/doctest.h>

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>

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
    // After release, the buffer should be counted as returned to the pool
    REQUIRE(counters.framebuffer_buffer_returned_to_pool_count.load() >= 1);
    REQUIRE(counters.framebuffer_reuses.load() == 0);
    // First acquire was a fresh allocation (pool empty) → empty_alloc, not exact_hit
    REQUIRE(counters.framebuffer_pool_empty_alloc.load() >= 1);
    REQUIRE(counters.framebuffer_pool_exact_hit.load() == 0);

    {
        auto b = pool->acquire_pooled(1920, 1080, pool);
    }
    // Second acquire should have reused the released framebuffer (exact hit)
    REQUIRE(counters.framebuffer_reuses.load() >= 1);
    REQUIRE(counters.framebuffer_buffer_returned_to_pool_count.load() >= 2);
    REQUIRE(counters.framebuffer_pool_exact_hit.load() >= 1);

    profiling::g_current_counters = nullptr;
}

// ── Priorità 4 Test 1: Framebuffer stride non multiplo ──────────────────

TEST_CASE("Framebuffer stride with non-multiple dimensions (1919x1079)") {
    Framebuffer fb(1919, 1079);

    // allocated_width must be >= width and cache-line aligned
    CHECK(fb.allocated_width() >= 1919);
    CHECK(fb.stride() == fb.allocated_width());

    // pixels_row must not go out-of-bounds
    for (i32 y = 0; y < fb.height(); ++y) {
        Color* row = fb.pixels_row(y);
        CHECK(row != nullptr);
        // Access last logical pixel of each row
        row[fb.width() - 1] = Color::blue();
    }

    // set_pixel/get_pixel on the last pixels works
    fb.set_pixel(1918, 1078, Color::red());
    Color c = fb.get_pixel(1918, 1078);
    CHECK(c.r == Color::red().r);
    CHECK(c.g == Color::red().g);
    CHECK(c.b == Color::red().b);
    CHECK(c.a == Color::red().a);

    // set_pixel/get_pixel on (0, 0)
    fb.set_pixel(0, 0, Color::green());
    CHECK(fb.get_pixel(0, 0).g == Color::green().g);

    // clear works (should not crash, all logical pixels become transparent)
    fb.clear(Color::transparent());
    CHECK(fb.get_pixel(1918, 1078).a == 0.0f);
    CHECK(fb.get_pixel(0, 0).a == 0.0f);
}

// ── Priorità 4 Test 2: resize_logical non rompe il buffer ──────────────

TEST_CASE("resize_logical does not break the buffer") {
    // Create Framebuffer 2048x1088 (power-of-two-like)
    Framebuffer fb(2048, 1088);
    i32 orig_alloc_w = fb.allocated_width();
    i32 orig_alloc_h = fb.allocated_height();
    size_t orig_bytes = fb.size_bytes();

    // resize_logical to smaller size
    fb.resize_logical(1919, 1079);

    // allocated_width must NOT shrink
    CHECK(fb.allocated_width() >= 1919);
    CHECK(fb.allocated_width() == orig_alloc_w);
    CHECK(fb.allocated_height() == orig_alloc_h);
    CHECK(fb.size_bytes() == orig_bytes);

    // Write pixel at the last logical position (1918, 1078)
    fb.set_pixel(1918, 1078, Color::red());

    // Read it back
    Color c = fb.get_pixel(1918, 1078);
    CHECK(c.r == 1.0f);

    // Clear
    fb.clear(Color::transparent());
    CHECK(fb.get_pixel(1918, 1078).a == 0.0f);

    // Write on multiple rows
    for (i32 y = 0; y < fb.height(); ++y) {
        fb.set_pixel(y % fb.width(), y, Color::white());
    }

    // Verify all rows (no out-of-bounds)
    for (i32 y = 0; y < fb.height(); ++y) {
        Color px = fb.get_pixel(y % fb.width(), y);
        CHECK(px.r == 1.0f);
    }

    // Grow beyond original
    fb.resize_logical(3000, 2000);
    CHECK(fb.width() == 3000);
    CHECK(fb.height() == 2000);
    CHECK(fb.allocated_width() >= 3000);
    CHECK(fb.allocated_height() == 2000);
    CHECK(fb.size_bytes() > orig_bytes);
}

// ── Priorità 4 Test 3: FramebufferPool reuse ───────────────────────────

TEST_CASE("FramebufferPool reuse - allocations stable after warmup") {
    auto pool = std::make_shared<FramebufferPool>(1024ULL * 1024ULL * 1024ULL); // 1 GB
    pool->reset_counters();

    // First acquire: should be an allocation
    auto fb1 = pool->acquire(1920, 1080);
    auto* ptr1 = fb1.get();
    auto stats1 = pool->stats();
    CHECK(stats1.total_allocations == 1);
    CHECK(stats1.total_reuses == 0);

    // Release it
    fb1.reset();
    auto stats_after_release = pool->stats();
    CHECK(stats_after_release.total_returns == 1);

    // Second acquire: should reuse
    auto fb2 = pool->acquire(1920, 1080);
    auto stats2 = pool->stats();
    CHECK(fb2.get() == ptr1);
    CHECK(stats2.total_reuses >= 1);
    CHECK(stats2.total_allocations == 1);  // No new allocation
    CHECK(stats2.hit_rate > 0.0);
}

TEST_CASE("FramebufferPool reuse with smaller logical size from same bucket") {
    auto pool = std::make_shared<FramebufferPool>(1024ULL * 1024ULL * 1024ULL);
    pool->reset_counters();

    // Acquire at bucket-rounded size 1920 (rounds to 1920 since 1920 % 64 == 0? 1920/4=480, 480%16=0, so 1920%4=0, 1920/64=30, yes aligned)
    auto fb1 = pool->acquire(1920, 1080);
    auto* ptr1 = fb1.get();
    fb1.reset();

    // Acquire smaller size that falls in same bucket
    auto fb2 = pool->acquire(1900, 1050);
    CHECK(fb2.get() == ptr1);  // Same buffer, different logical size
    CHECK(fb2->width() == 1900);
    CHECK(fb2->height() == 1050);

    // allocated_width should still be >= 1920 (the bucket size)
    CHECK(fb2->allocated_width() >= 1920);
}
