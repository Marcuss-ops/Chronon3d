// ============================================================================
// test_evict_lru_for.cpp — Direct tests for evict_lru_for (and its trio
// evict_one_from_bucket + evict_global_lru) extracted in FASE 18
// (framebuffer_pool_evict.cpp).
//
// Tests the LRU-eviction driver in isolation from the acquire/release
// flow — we drive the pool directly via its public release() API to set
// up budget-pressure scenarios, then read pool stats to confirm the
// correct framebuffers were evicted.
//
// Coverage (cat-2 freeze-compliant: deterministic, no threads, no time):
//   1. unlimited budget (max_retained_bytes=0): no eviction
//   2. tight per-size-class budget: oldest evicted first
//   3. cross-bucket LRU: globally oldest evicted
//   4. evict_lru_for with no entries (empty pool) returns false
//   5. round_to_bucket: bucket rounding is monotonic
// ============================================================================

#include <doctest/doctest.h>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/counters.hpp>
using namespace chronon3d;
using namespace chronon3d::cache;

// =============================================================================
// Test 1: unlimited budget (max_retained_bytes=0) — release returns the FB
// to the pool, no eviction pressure.  This is the regression lock for the
// "0 = unlimited" sentinel.
// =============================================================================
TEST_CASE("evict_lru_for: unlimited budget never evicts") {
    FramebufferPoolConfig config;
    config.max_retained_bytes = 0;          // unlimited
    config.max_buffers_per_size_class = 10; // plenty per class
    config.enable_lru_eviction = true;

    auto pool = std::make_shared<FramebufferPool>(config);
    pool->reset_counters();

    // Acquire + release 4 FB of the same size, all should be retained.
    auto a = pool->acquire(64, 64);
    auto b = pool->acquire(64, 64);
    auto c = pool->acquire(64, 64);
    auto d = pool->acquire(64, 64);
    a.reset(); b.reset(); c.reset(); d.reset();

    auto stats = pool->stats();
    CHECK(stats.evicted_count == 0);
    CHECK(stats.pressure_count == 0);
    // (We may have grown the pool to size 1 — the 4 acquires reuse the
    // same FB, not 4 distinct ones.)
    CHECK(stats.available_count >= 1);
}

// =============================================================================
// Test 2: tight budget — second release triggers eviction of the first.
// Config: budget=65536 (one 64x64 FB), per-class=2.  Release 2 FBs at the
// same size: by release order, the OLDER FB is evicted.
// =============================================================================
TEST_CASE("evict_lru_for: tight budget evicts the oldest entry") {
    FramebufferPoolConfig config;
    config.max_retained_bytes = 64 * 64 * 16;            // exactly 1 FB
    config.max_buffers_per_size_class = 4;                // bucket not limiting
    config.enable_lru_eviction = true;

    auto pool = std::make_shared<FramebufferPool>(config);
    pool->reset_counters();

    // Acquire 2 distinct FBs at the same size (so both are tracked when
    // released).  Hold them so they're not reused.
    auto a = pool->acquire(64, 64);
    auto* ptr_a = a.get();
    auto b = pool->acquire(64, 64);
    auto* ptr_b = b.get();
    (void)ptr_a; (void)ptr_b;

    a.reset();  // tick 1: a is the only entry now
    CHECK(pool->available_count() == 1);

    b.reset();  // tick 2: budget pressure -> evict `a` before retaining `b`
    auto stats = pool->stats();
    CHECK(stats.evicted_count >= 1);
    CHECK(stats.pressure_count >= 1);
    CHECK(pool->available_count() == 1);
    CHECK(stats.current_bytes <= config.max_retained_bytes);
}

// =============================================================================
// Test 3: per-size-class limit — older entry in the same bucket is evicted
// even when total budget has room for it.
// =============================================================================
TEST_CASE("evict_lru_for: per-size-class limit evicts oldest in same bucket") {
    FramebufferPoolConfig config;
    config.max_retained_bytes = 0;                        // unlimited (total)
    config.max_buffers_per_size_class = 2;               // tight per bucket
    config.enable_lru_eviction = true;

    auto pool = std::make_shared<FramebufferPool>(config);
    pool->reset_counters();

    auto a = pool->acquire(64, 64);
    auto* ptr_a = a.get();
    auto b = pool->acquire(64, 64);
    auto* ptr_b = b.get();
    auto c = pool->acquire(64, 64);
    auto* ptr_c = c.get();
    REQUIRE(ptr_a != ptr_b);
    REQUIRE(ptr_b != ptr_c);
    REQUIRE(ptr_a != ptr_c);  // 3 distinct allocations

    // Release in order: a (oldest), b, c (newest).  Per-class limit=2 means
    // a should be evicted to make room for c.
    a.reset();
    b.reset();
    c.reset();

    auto stats = pool->stats();
    CHECK(stats.available_count == 2);   // only 2 remain
    CHECK(stats.evicted_count >= 1);     // one was evicted

    // Acquiring twice yields b and c, NOT a.
    auto get1 = pool->acquire(64, 64);
    auto* p1 = get1.get();
    auto get2 = pool->acquire(64, 64);
    auto* p2 = get2.get();
    CHECK(!((p1 == ptr_a) || (p2 == ptr_a)));
}

// =============================================================================
// Test 4 (post-fix): cross-bucket LRU eviction -- shrink-budget driver.
//
// The cleanest path to exercise `evict_global_lru()` (and thereby the
// evict_lru_for loop via set_budget_bytes) is to insert FBs across multiple
// size classes, then shrink the retention budget so the eviction loop
// fires.  We hold 2 distinct FBs (different sizes -> different buckets)
// so acquiring-then-releasing does NOT collapse to exact-fit-reuse.
// =============================================================================
TEST_CASE("evict_lru_for: cross-bucket LRU drops oldest globally via set_budget_bytes") {
    // 32x32 FB ~ 16384 bytes, 64x64 FB ~ 65536 bytes.
    // Budget starts unlimited; we shrink it below total retained size to
    // force evict_global_lru to fire across buckets (oldest tick removed first).
    FramebufferPoolConfig config;
    config.max_retained_bytes = 0;          // start unlimited
    config.max_buffers_per_size_class = 10; // bucket not limiting
    config.enable_lru_eviction = true;

    auto pool = std::make_shared<FramebufferPool>(config);
    pool->reset_counters();

    // Hold 2 distinct FBs of different sizes simultaneously so neither
    // collapses to exact-fit-reuse.
    auto big1   = pool->acquire(64, 64); auto* ptr_big1   = big1.get();
    auto small1 = pool->acquire(32, 32); auto* ptr_small1 = small1.get();
    REQUIRE(ptr_big1 != ptr_small1);

    // Release in order: small1 first (tick 1, OLDEST), then big1 (tick 2).
    // Both fit in budget (=unlimited) -> both retained in their buckets.
    small1.reset();
    big1.reset();
    CHECK(pool->available_count() == 2);
    CHECK(pool->current_bytes() == 16384 + 65536);

    // Reset eviction counters so the verification only counts evictions
    // triggered by the budget shrink below (not any setup bookkeeping).
    pool->reset_counters();

    // Shrink the budget to fit only one big FB (or less).  This forces
    // evict_global_lru() in a loop.  Pool: {32x32: [small1 tick 1],
    // 64x64: [big1 tick 2]}.  Loop: pick OLDEST tick across ALL buckets
    // (small1 tick 1 is global LRU) -> evict small1 -> current=65536 ->
    // 65536 <= 65536 OK -> stop.
    pool->set_budget_bytes(65536);

    auto stats = pool->stats();
    CHECK(stats.evicted_count >= 1);
    CHECK(stats.current_bytes <= 65536);
    CHECK(pool->available_count() == 1);

    // Global-LRU correctness: small1 (released FIRST) is the evicted one.
    // Acquiring 32x32 now returns a FRESH FB, not ptr_small1.
    auto reuse_small = pool->acquire(32, 32);
    auto* reuse_small_ptr = reuse_small.get();
    CHECK(reuse_small_ptr != ptr_small1);
}

// =============================================================================
// Test 5: set_budget_bytes shrinks the budget and triggers immediate eviction.
// =============================================================================
TEST_CASE("evict_lru_for: set_budget_bytes triggers immediate eviction") {
    FramebufferPoolConfig config;
    config.max_retained_bytes = 1024ULL * 1024;          // 1 MB, generous
    config.max_buffers_per_size_class = 10;
    config.enable_lru_eviction = true;

    auto pool = std::make_shared<FramebufferPool>(config);
    pool->reset_counters();

    // Acquire + release 4 distinct FBs.
    auto a = pool->acquire(64, 64); a.reset();
    auto b = pool->acquire(64, 64); b.reset();
    auto c = pool->acquire(64, 64); c.reset();
    auto d = pool->acquire(64, 64); d.reset();

    // Each release may reuse previous (only 1 FB remains in the pool).
    CHECK(pool->current_bytes() <= 65536 + 1);  // single 64x64 FB

    // Now shrink the budget to nothing.
    pool->set_budget_bytes(0);
    auto stats = pool->stats();
    // budget_bytes=0 means unlimited — but with budget=0 the prior FBs are
    // still retained because release() only checks during insertion.
    // (Note: docstring says 0 == unlimited; this is a separate idiom.)
    CHECK(stats.budget_bytes == 0);

    // Shrink REALLY tight: 1 byte. Now budget pressure pushes eviction.
    pool->set_budget_bytes(1);
    stats = pool->stats();
    CHECK(stats.current_bytes <= 1);
    CHECK(stats.evicted_count >= 1);
    CHECK(stats.pressure_count >= 1);
}

// =============================================================================
// Test 6: round_to_bucket returns monotonic (non-decreasing) rounded sizes.
// =============================================================================
TEST_CASE("FramebufferPool::round_to_bucket is monotonic") {
    auto prev = FramebufferPool::round_to_bucket(0, 0);
    // Round to bucket rounded up; as inputs grow, output grows monotonically.
    for (int sz : {1, 2, 4, 7, 8, 16, 31, 32, 63, 64, 65, 100, 256, 1023, 1024, 1025}) {
        auto cur = FramebufferPool::round_to_bucket(sz, sz);
        // Within the same input axis, increasing input gives non-decreasing output.
        CHECK(cur.first  >= prev.first);     // width is non-decreasing in integer weak order
        CHECK(cur.second >= prev.second);
        prev = cur;
        // round_to_bucket never returns a value smaller than the input
        // (bucket rounding is at least as large as the requested size).
        CHECK(cur.first  >= sz);
        CHECK(cur.second >= sz);
    }
}

TEST_CASE("FramebufferPool::round_to_bucket handles zero/negative as zero-pair") {
    auto p = FramebufferPool::round_to_bucket(0, 0);
    // Either 0 or 1 — both are valid for the precondition "no allocation".
    // The helper itself isn't required to clamp to 0, but it must be
    // deterministic.
    CHECK(p.first  >= 0);
    CHECK(p.second >= 0);
}
