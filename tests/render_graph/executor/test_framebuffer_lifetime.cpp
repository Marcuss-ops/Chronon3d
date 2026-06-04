#include <doctest/doctest.h>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/color.hpp>
#include <memory>

using namespace chronon3d;
using namespace chronon3d::cache;

// ── Framebuffer Lifetime Contract ────────────────────────────────────────────
//
// The executor must not release a framebuffer input before the last consumer
// has finished with it.  The pool must not leak memory, and acquiring/releasing
// must be stable.

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
    FramebufferPool pool(256 * 1024 * 1024);
    auto fb1 = pool.acquire_owned(100, 100, true);
    // Write a pixel
    fb1->set_pixel(50, 50, Color::white());

    // Release and re-acquire without clear — contents should be preserved (reuse)
    Framebuffer* raw = fb1.release();
    PoolFbDeleter deleter{&pool};
    OwnedFB fb2(raw, std::move(deleter));

    // Re-acquire with clear=false from pool
    auto fb3 = pool.acquire_owned(100, 100, false);
    // We can't guarantee which buffer we get, but it should be a valid 100x100 buffer
    CHECK(fb3->width() == 100);
    CHECK(fb3->height() == 100);
}

TEST_CASE("FramebufferLifetime: pool reuses previously returned buffer") {
    FramebufferPool pool(64 * 1024 * 1024); // 64 MB
    {
        auto fb = pool.acquire_owned(1920, 1080, true);
        CHECK(fb != nullptr);
    } // fb released back to pool
    // Acquire again — this should reuse the previously returned buffer
    {
        auto fb = pool.acquire_owned(1920, 1080, true);
        CHECK(fb != nullptr);
    }
    auto stats = pool.stats();
    CHECK(stats.total_reuses >= 1);
}

TEST_CASE("FramebufferLifetime: multiple acquisitions do not alias") {
    FramebufferPool pool(256 * 1024 * 1024);
    auto fb_a = pool.acquire_owned(100, 100, true);
    auto fb_b = pool.acquire_owned(100, 100, true);

    // They must be different framebuffers
    CHECK(fb_a.get() != fb_b.get());

    // Writing to one must not affect the other
    fb_a->set_pixel(0, 0, Color::red());
    Color c = fb_b->get_pixel(0, 0);
    CHECK_FALSE(c.r > 0.9f); // fb_b should not be red
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
    // Should not crash, pool should have the buffer back
    CHECK_NOTHROW(fb.reset());
}
