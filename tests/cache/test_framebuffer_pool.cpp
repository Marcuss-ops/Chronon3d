#include <doctest/doctest.h>

#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <thread>
#include <vector>
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

// ── Pool budget tests ────────────────────────────────────────────────────

TEST_CASE("FramebufferPool budget respected — eviction on release") {
    // 64x64 framebuffer = 64*64*16 = 65536 bytes ≈ 64KB
    // Budget allows 2 framebuffers (128KB)
    FramebufferPoolConfig config;
    config.max_retained_bytes = 65536 * 2;
    config.max_buffers_per_size_class = 10;  // high enough to not trigger per-class limit
    config.enable_lru_eviction = true;

    auto pool = std::make_shared<FramebufferPool>(config);
    pool->reset_counters();

    // Release 3 framebuffers — only 2 should fit
    auto fb1 = pool->acquire(64, 64);
    auto fb2 = pool->acquire(64, 64);
    auto fb3 = pool->acquire(64, 64);
    fb1.reset();
    fb2.reset();
    fb3.reset();

    auto stats = pool->stats();
    CHECK(stats.current_bytes <= config.max_retained_bytes);
    CHECK(stats.available_count == 2);
    CHECK(stats.evicted_count >= 1);  // one FB was evicted
    CHECK(stats.pressure_count >= 1);
}

TEST_CASE("FramebufferPool exact hit still works with budget") {
    FramebufferPoolConfig config;
    config.max_retained_bytes = 100ULL * 1024 * 1024;  // 100MB — plenty for 1920×1080 (≈32MB)
    config.max_buffers_per_size_class = 4;

    auto pool = std::make_shared<FramebufferPool>(config);
    pool->reset_counters();

    // First acquire = fresh alloc
    auto fb1 = pool->acquire(1920, 1080);
    auto* ptr1 = fb1.get();
    auto stats1 = pool->stats();
    CHECK(stats1.total_allocations == 1);

    // Release then re-acquire = exact hit
    fb1.reset();
    auto fb2 = pool->acquire(1920, 1080);
    auto stats2 = pool->stats();
    CHECK(fb2.get() == ptr1);
    CHECK(stats2.total_reuses == 1);
    CHECK(stats2.total_allocations == 1);  // no new alloc
}

TEST_CASE("FramebufferPool LRU eviction — oldest entry evicted first") {
    // Test per-size-class eviction: limit 2 per bucket, release 3 FBs.
    // The oldest (lowest tick) should be evicted from the bucket.
    FramebufferPoolConfig config;
    config.max_retained_bytes = 10ULL * 1024 * 1024;  // large — not the limiting factor
    config.max_buffers_per_size_class = 2;  // only 2 per bucket triggers eviction
    config.enable_lru_eviction = true;

    auto pool = std::make_shared<FramebufferPool>(config);
    pool->reset_counters();

    // Hold 3 distinct FBs simultaneously so they all survive on release.
    auto hold_a = pool->acquire(64, 64);
    auto* ptr_a = hold_a.get();
    auto hold_b = pool->acquire(64, 64);
    auto* ptr_b = hold_b.get();
    auto hold_c = pool->acquire(64, 64);
    auto* ptr_c = hold_c.get();
    CHECK(ptr_a != ptr_b);
    CHECK(ptr_b != ptr_c);
    CHECK(ptr_a != ptr_c);  // 3 distinct allocations

    // Release A first (oldest), then B, then C (newest).
    hold_a.reset();  // tick: A=1
    hold_b.reset();  // tick: B=2
    hold_c.reset();  // tick: C=3 — per-class limit exceeded, oldest (A) evicted

    auto stats = pool->stats();
    CHECK(stats.available_count == 2);   // only 2 remain (limit per class)
    CHECK(stats.evicted_count >= 1);     // one FB was evicted
    CHECK(stats.pressure_count >= 1);

    // The remaining two should be B and C (A was evicted as oldest).
    // Acquire twice and check we don't get A back.
    auto get1 = pool->acquire(64, 64);
    auto* p1 = get1.get();
    auto get2 = pool->acquire(64, 64);
    auto* p2 = get2.get();

    bool got_a = (p1 == ptr_a || p2 == ptr_a);
    CHECK(!got_a);  // A should have been evicted
}

TEST_CASE("FramebufferPool per-size-class limit enforced") {
    FramebufferPoolConfig config;
    config.max_retained_bytes = 10ULL * 1024 * 1024;  // plenty of room
    config.max_buffers_per_size_class = 2;  // only 2 per bucket
    config.enable_lru_eviction = true;

    auto pool = std::make_shared<FramebufferPool>(config);
    pool->reset_counters();

    // Release 3 framebuffers of the same size
    auto fb1 = pool->acquire(128, 128);
    auto fb2 = pool->acquire(128, 128);
    auto fb3 = pool->acquire(128, 128);
    fb1.reset();
    fb2.reset();
    fb3.reset();

    auto stats = pool->stats();
    // Should have at most 2 in this size class
    CHECK(stats.available_count == 2);
    CHECK(stats.evicted_count >= 1);
}

TEST_CASE("FramebufferPool set_budget_bytes shrinks pool immediately") {
    FramebufferPoolConfig config;
    config.max_retained_bytes = 10ULL * 1024 * 1024;  // start generous
    config.max_buffers_per_size_class = 10;

    auto pool = std::make_shared<FramebufferPool>(config);

    // Release 3 FBs (~192KB)
    auto fb1 = pool->acquire(64, 64);
    auto fb2 = pool->acquire(64, 64);
    auto fb3 = pool->acquire(64, 64);
    fb1.reset();
    fb2.reset();
    fb3.reset();
    CHECK(pool->available_count() == 3);

    // Shrink budget to only fit 1 FB
    pool->set_budget_bytes(65536);

    auto stats = pool->stats();
    CHECK(stats.available_count <= 1);
    CHECK(stats.current_bytes <= 65536);
    CHECK(stats.budget_bytes == 65536);
    CHECK(stats.pressure_count >= 1);
}

TEST_CASE("FramebufferPool budget unlimited (0) never evicts") {
    FramebufferPoolConfig config;
    config.max_retained_bytes = 0;  // unlimited
    config.max_buffers_per_size_class = 10;

    auto pool = std::make_shared<FramebufferPool>(config);
    pool->reset_counters();

    // Acquire and release 5 FBs of the same size.  Each release retains the FB
    // but the next acquire reuses it, so the pool keeps only 1 FB.
    auto* last_ptr = static_cast<Framebuffer*>(nullptr);
    for (int i = 0; i < 5; ++i) {
        auto fb = pool->acquire(64, 64);
        if (i == 0) last_ptr = fb.get();
        // Each acquire reuses the same FB; ptr should be the same
        CHECK(fb.get() == last_ptr);
    }

    auto stats = pool->stats();
    CHECK(stats.available_count == 1);  // one FB reused 5 times
    CHECK(stats.total_reuses == 4);     // reused 4 times after first alloc
    CHECK(stats.evicted_count == 0);
    CHECK(stats.pressure_count == 0);
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

// ── acquire_noclear: byte-identical-equivalence test ────────────────
//
// Verifies the contract: `acquire_noclear(w, h)` followed by the
// caller-contract `fb->clear(transparent)` produces pixel-by-pixel
// byte-identical output to `acquire(w, h, clear=true)` (the existing
// default-cleared path).  Internally the two paths share the same
// `acquire_unique()` helper, so the bytes-after-clear must match —
// this test pins that property so future refactors cannot drift the
// two paths apart.
//
// SECURITY NOTE: the test exercises both `acquire_noclear` and
// `acquire` in the SAME process.  The default `acquire` path zeroes
// before returning, so the `acquire_noclear` test branch's manual
// clear takes the FB to the same end state as the existing path.
// The no-info-leak contract is documented in the header — this test
// simply verifies bit-equivalence after manual clear.
TEST_CASE("FramebufferPool::acquire_noclear + manual clear == acquire (byte-identical)") {
    auto pool = std::make_shared<FramebufferPool>(1024ULL * 1024ULL);
    pool->reset_counters();

    constexpr int kW = 64;
    constexpr int kH = 64;
    constexpr std::size_t kPayloadBytes =
        static_cast<std::size_t>(kW) * kH * sizeof(Color);

    // ── Path A: default acquire (acquire(w, h, clear=true) preset). ──
    // FB is freshly allocated; is_content_cleared() == true at the
    // wrapper boundary, so the O(w*h) full-clear is skipped on this
    // path — but the test compares CONTENT, so this is a fair baseline.
    auto fb_default = pool->acquire(kW, kH);
    REQUIRE(fb_default != nullptr);
    REQUIRE(fb_default->data() != nullptr);

    // Currently held (NOT released), so the second acquire from the
    // pool's perspective is a fresh_alloc.  To exercise the hot-miss
    // path (where stale content would be a concern), release Path A,
    // then run Path B which will reuse Path A's buffer via best-fit.
    auto* ptr_default = fb_default.get();
    fb_default.reset();

    // ── Path B: acquire_noclear + manual clear. ──
    // The first acquire_noclear after Path A's release goes through
    // acquire_unique, hits the exact bucket, gets ptr_default back,
    // and SKIPS the wrapper's clear (acquire_noclear doesn't clear).
    // Steps:
    //   1. acquire_noclear — gets ptr_default (the same buffer Path A used).
    //   2. The pixel content is whatever Path A wrote (transparent).
    //   3. Manually fb->clear(transparent) — same colour as the
    //      default-path wrapper would have done.
    //   4. The two FBs are now byte-identical (both all-transparent).
    auto fb_noclear = pool->acquire_noclear(kW, kH);
    REQUIRE(fb_noclear != nullptr);
    REQUIRE(fb_noclear->data() != nullptr);

    // The acquire_noclear path on the SAME pool reuses the same FB
    // pointer (= ptr_default) — that's the hot-miss reuse path that
    // the new variant is designed to accelerate.
    CHECK(fb_noclear.get() == ptr_default);

    // Caller honors the security contract: explicit clear before reading.
    fb_noclear->clear(Color::transparent());

    // Structural parity.
    CHECK(fb_noclear->width()  == kW);
    CHECK(fb_noclear->height() == kH);
    CHECK(fb_noclear->allocated_width() >= kW);
    CHECK(fb_noclear->allocated_height() >= kH);

    // ── Byte-by-byte payload equality (the core of the test). ──
    // Allocate a third FB at the SAME size, take the cleared default
    // path on a SECOND default-acquire (which will reuse ptr_default
    // a second time — Path A's logic cleared it on the previous
    // acquire), and compare against the manually-cleared noclear FB.
    // Both paths end on transparent (the wrapper's clear colour is
    // `Color::transparent()`, matching the manual `clear` we did).
    auto fb_default_b = pool->acquire(kW, kH);   // may be a fresh alloc or reuse
    REQUIRE(fb_default_b != nullptr);
    REQUIRE(fb_default_b->data() != nullptr);
    CHECK(std::memcmp(fb_default_b->data(), fb_noclear->data(), kPayloadBytes) == 0);

    // Spot-check each pixel via the canonical accessors — round out
    // the structural verification with a row-by-row + col-by-col walk.
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const Color c_def = fb_default_b->get_pixel(x, y);
            const Color c_nc  = fb_noclear->get_pixel(x, y);
            CHECK(c_def.r == c_nc.r);
            CHECK(c_def.g == c_nc.g);
            CHECK(c_def.b == c_nc.b);
            CHECK(c_def.a == c_nc.a);
        }
    }

    // ── Counter parity. ──
    // Acquire_noclear MUST NOT trigger framebuffer_pool_clear_wall_ms —
    // its purpose is precisely to skip the clear.  The default path
    // (second acquire) ALSO skipped clear on this run (because the FB
    // is_content_cleared() from the manual clear).  Net: zero clears
    // counted in this test, matching the expected behaviour.
    auto stats = pool->stats();
    CHECK(stats.total_clears == 0);
}

// ═════════════════════════════════════════════════════════════════════════
// P1-21: FramebufferPoolClearPolicy + trim_after_job() regression tests
// ═════════════════════════════════════════════════════════════════════════

TEST_CASE("FramebufferPool: trim_after_job honors FramebufferPoolClearPolicy") {
    using chronon3d::cache::FramebufferPool;
    using chronon3d::cache::FramebufferPoolClearPolicy;

    SUBCASE("KeepWarm: trim_after_job is a no-op") {
        FramebufferPool pool(64ULL * 1024ULL * 1024ULL);
        pool.set_clear_policy(FramebufferPoolClearPolicy::KeepWarm);

        // Acquire and release a framebuffer to populate the pool.
        // Use acquire_noclear + release(raw.release()) so the raw pointer
        // is returned to the pool exactly once (avoids double-release via
        // the shared_ptr deleter in acquire()).
        auto fb = pool.acquire_noclear(64, 64);
        pool.release(fb.release());
        REQUIRE(pool.available_count() >= 1);

        // trim_after_job must NOT clear (KeepWarm preserves warm state).
        pool.trim_after_job();
        CHECK(pool.available_count() >= 1);
    }

    SUBCASE("TrimAfterJob: trim_after_job calls clear()") {
        FramebufferPool pool(64ULL * 1024ULL * 1024ULL);
        pool.set_clear_policy(FramebufferPoolClearPolicy::TrimAfterJob);

        auto fb = pool.acquire_noclear(64, 64);
        pool.release(fb.release());
        REQUIRE(pool.available_count() >= 1);

        // trim_after_job MUST clear (TrimAfterJob drops all pooled FBs).
        pool.trim_after_job();
        CHECK(pool.available_count() == 0);
    }

    SUBCASE("TrimOnMemoryPressure: trim_after_job is a no-op (LRU handles it)") {
        FramebufferPool pool(64ULL * 1024ULL * 1024ULL);
        pool.set_clear_policy(FramebufferPoolClearPolicy::TrimOnMemoryPressure);

        auto fb = pool.acquire_noclear(64, 64);
        pool.release(fb.release());
        REQUIRE(pool.available_count() >= 1);

        // trim_after_job must NOT clear (TrimOnMemoryPressure relies on
        // automatic LRU eviction when max_retained_bytes is exceeded).
        pool.trim_after_job();
        CHECK(pool.available_count() >= 1);
    }

    SUBCASE("set_clear_policy / clear_policy round-trip") {
        FramebufferPool pool(64ULL * 1024ULL * 1024ULL);
        REQUIRE(pool.clear_policy() == FramebufferPoolClearPolicy::TrimAfterJob);  // default (matches pre-P1-21 hardcoded clear() behavior)

        pool.set_clear_policy(FramebufferPoolClearPolicy::KeepWarm);
        CHECK(pool.clear_policy() == FramebufferPoolClearPolicy::KeepWarm);

        pool.set_clear_policy(FramebufferPoolClearPolicy::TrimAfterJob);
        CHECK(pool.clear_policy() == FramebufferPoolClearPolicy::TrimAfterJob);
    }
}

TEST_CASE("Config: framebuffer_pool_clear_policy accessor + setter") {
    auto cfg = chronon3d::Config::from_environment();

    // Default is TrimAfterJob (matches pre-P1-21 production behavior:
    // pipe_export_loop unconditionally called clear() at the end of every job).
    CHECK(cfg.cache().framebuffer_pool_clear_policy() ==
          chronon3d::cache::FramebufferPoolClearPolicy::TrimAfterJob);

    // set_fb_pool_clear_policy overrides the default.
    cfg.set_fb_pool_clear_policy(chronon3d::cache::FramebufferPoolClearPolicy::KeepWarm);
    CHECK(cfg.cache().framebuffer_pool_clear_policy() ==
          chronon3d::cache::FramebufferPoolClearPolicy::KeepWarm);

    cfg.set_fb_pool_clear_policy(chronon3d::cache::FramebufferPoolClearPolicy::TrimAfterJob);
    CHECK(cfg.cache().framebuffer_pool_clear_policy() ==
          chronon3d::cache::FramebufferPoolClearPolicy::TrimAfterJob);
}

// Wave 2 — contention/rotation telemetry. acquire/release/set_arena all cross
// the same pool mutex; the pool must account every one of them and report the
// aggregate wait/hold microseconds so the render loop can tell lock
// contention apart from pool work (fb_pool_lock_wait_ms / arena_set_count
// acceptance: arena_set_count is a per-call meter that the Wave-4 job-scoped
// association must drive toward ~1/job instead of frames/job).
// Wave 4 — job-scoped arena association (see attach_arena_provider). The
// render loop attaches ONE provider per job and publishes the rotating
// triple-buffer slot before each frame; the pool consults the provider (under
// its own mutex) only when a fresh allocation misses the free list. Acceptance:
//   * association is a job-lifetime event  → arena_set_count ≈ 1/job, NOT
//     frames/job (per-frame publish must never reconfigure the pool)
//   * allocations for the current frame come from the published slot
//   * detach restores the static/heap fallback (daemon reuses the pool across
//     jobs; a stale provider must never outlive the job that installed it)
TEST_CASE("FramebufferPool wave-4 job-scoped arena provider drives per-frame slots") {
    // Shared-ownership pool so acquire()'s deleter returns FBs through
    // pool->release() exactly like production.
    auto pool = FramebufferPool::create_shared(256ULL * 1024ULL * 1024ULL);
    auto slot_a = std::make_shared<FramebufferArena>(4ULL * 1024ULL * 1024ULL);
    auto slot_b = std::make_shared<FramebufferArena>(4ULL * 1024ULL * 1024ULL);
    REQUIRE(slot_a->used_bytes() == 0);
    REQUIRE(slot_b->used_bytes() == 0);

    // Mirrors JobArenaAssociation in pipe_export_render_loop.cpp: the pool
    // association is a job-scoped provider and per-frame rotation is a plain
    // atomic publish (no pool call, no mutex, no arena_set_count growth).
    std::atomic<FramebufferArena*> current_slot{nullptr};

    pool->reset_counters();
    pool->attach_arena_provider([&current_slot]() {
        return current_slot.load(std::memory_order_acquire);
    });

    // Association is exactly one tracked event per job.
    auto after_attach = pool->stats();
    CHECK(after_attach.arena_set_count == 1);

    // Frame 1: publish slot A, allocate → storage must come from slot A.
    current_slot.store(slot_a.get(), std::memory_order_release);
    {
        auto fb = pool->acquire(64, 64);
        REQUIRE(fb != nullptr);
        CHECK(fb->is_arena_allocated());
        CHECK(fb->data() != nullptr);
    }
    CHECK(slot_a->used_bytes() > 0);
    CHECK(slot_b->used_bytes() == 0);

    // Frame 2: publish slot B, allocate → storage must come from slot B
    // (proving the pool re-reads the provider per frame instead of caching
    // the first arena).
    current_slot.store(slot_b.get(), std::memory_order_release);
    {
        auto fb = pool->acquire(64, 64);
        REQUIRE(fb != nullptr);
        CHECK(fb->is_arena_allocated());
    }
    CHECK(slot_b->used_bytes() > 0);
    const auto slot_a_after = slot_a->used_bytes();
    const auto slot_b_after = slot_b->used_bytes();
    CHECK(slot_a_after > 0);
    CHECK(slot_b_after > 0);

    // Per-frame rotation is NOT a pool reconfiguration: two frames allocated
    // but the pool was associated exactly once (arena_set_count stays 1).
    auto after_frames = pool->stats();
    CHECK(after_frames.arena_set_count == 1);
    CHECK(after_frames.acquire_count == 2);
    CHECK(after_frames.release_count == 2);  // arena FBs drop on release

    // Detach restores the fallback: a daemon reuses the pool across jobs, so
    // after the job ends, allocations must no longer consult the provider's
    // (now-dangling) slot. detach itself is not an association event.
    pool->detach_arena_provider();
    auto after_detach = pool->stats();
    CHECK(after_detach.arena_set_count == 1);
    {
        current_slot.store(slot_a.get(), std::memory_order_release);
        auto fb = pool->acquire(64, 64);
        REQUIRE(fb != nullptr);
        // No provider, no static arena → heap-backed fallback, exactly the
        // legacy behavior for callers that never installed an arena.
        CHECK(!fb->is_arena_allocated());
    }

    // Idempotent detach is safe.
    pool->detach_arena_provider();

    // A second job attaches fresh and pays exactly one more association.
    pool->attach_arena_provider([&current_slot]() {
        return current_slot.load(std::memory_order_acquire);
    });
    CHECK(pool->stats().arena_set_count == after_detach.arena_set_count + 1);
    pool->detach_arena_provider();
}

TEST_CASE("FramebufferPool wave-2 contention counters advance") {
    // Shared-ownership pool: acquire()'s shared deleter returns buffers via
    // pool->release(), which is exactly the release path being accounted.
    auto pool = FramebufferPool::create_shared(256ULL * 1024ULL * 1024ULL);
    const auto arena = std::make_shared<FramebufferArena>(4ULL * 1024ULL * 1024ULL);

    const auto before = pool->stats();
    pool->set_arena(arena);
    pool->set_arena(arena);  // second association is still one tracked call
    for (int i = 0; i < 100; ++i) {
        auto fb = pool->acquire(32, 32);
        REQUIRE(fb != nullptr);
        // fb released at scope end -> pool->release()
    }
    const auto after = pool->stats();

    CHECK(after.acquire_count >= before.acquire_count + 100);
    CHECK(after.release_count >= before.release_count + 100);
    CHECK(after.arena_set_count == before.arena_set_count + 2);
    // Locking overhead is never negative.
    CHECK(after.lock_wait_us >= before.lock_wait_us);
    CHECK(after.lock_hold_wall_us >= before.lock_hold_wall_us);
    CHECK(after.arena_set_wait_us >= before.arena_set_wait_us);
}

TEST_CASE("FramebufferPool narrow-before-shard measurement") {
    auto pool = FramebufferPool::create_shared(256ULL * 1024ULL * 1024ULL);
    pool->reset_counters();
    constexpr int kThreads = 8;
    constexpr int kIterations = 128;
    std::atomic<bool> acquired{true};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int thread = 0; thread < kThreads; ++thread) {
        workers.emplace_back([&pool, &acquired] {
            for (int i = 0; i < kIterations; ++i) {
                auto fb = pool->acquire(64, 64, false);
                if (!fb) acquired.store(false, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers) worker.join();

    const auto measured = pool->stats();
    CHECK(acquired.load(std::memory_order_relaxed));
    CHECK(measured.acquire_count == static_cast<std::uint64_t>(kThreads * kIterations));
    CHECK(measured.release_count == static_cast<std::uint64_t>(kThreads * kIterations));
    std::printf("[INFO] framebuffer-pool-narrow-before-shard: acquire=%llu wait_us=%llu hold_us=%llu\n",
                static_cast<unsigned long long>(measured.acquire_count),
                static_cast<unsigned long long>(measured.lock_wait_us),
                static_cast<unsigned long long>(measured.lock_hold_wall_us));
}
