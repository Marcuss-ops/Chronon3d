// tests/core/test_depth_buffer_pool.cpp
// ════════════════════════════════════════════════════════════════════════════
// Certifies the DepthBufferPool contract:
//   - acquire(w,h) returns a zeroed span of exactly w*h floats
//   - repeated acquire() at the same size reuses the same storage
//     (no reallocation — the pool's whole purpose)
//   - acquire() with a larger size grows the buffer and zeroes it
//   - bucket rounding means small size deltas reuse the same storage
//   - reset() releases the storage (allocator can reclaim it)
//   - move semantics work (SoftwareSessionResources relies on defaulted move)
// ════════════════════════════════════════════════════════════════════════════

#include <chronon3d/backends/software/depth_buffer_pool.hpp>

#include <doctest/doctest.h>

namespace {

} // namespace

TEST_CASE("DepthBufferPool: acquire returns exactly w*h zeroed floats") {
    chronon3d::DepthBufferPool pool;
    auto span = pool.acquire(100, 50);
    CHECK_EQ(span.size(), static_cast<size_t>(100) * 50);
    CHECK(span.data() != nullptr);
    // Must be zero-initialized (depth test starts from "nothing written").
    for (size_t i = 0; i < span.size(); ++i) {
        CHECK_EQ(span[i], 0.0f);
    }
}

TEST_CASE("DepthBufferPool: same-size acquire reuses storage (no realloc)") {
    chronon3d::DepthBufferPool pool;
    auto first = pool.acquire(640, 480);
    auto second = pool.acquire(640, 480);
    // Pointer stability ⇒ no per-frame allocation on the steady-state path.
    CHECK_EQ(first.data(), second.data());
    // Re-acquire zeroes the buffer again.
    for (size_t i = 0; i < second.size(); ++i) {
        CHECK_EQ(second[i], 0.0f);
    }
}

TEST_CASE("DepthBufferPool: larger acquire grows the buffer") {
    chronon3d::DepthBufferPool pool;
    auto small = pool.acquire(10, 10);
    auto big = pool.acquire(2000, 2000);
    CHECK_EQ(big.size(), static_cast<size_t>(2000) * 2000);
    // Storage must have been reallocated (capacity grew past small).
    CHECK(big.data() != small.data());
    // Newly-grown region is zeroed.
    CHECK_EQ(big[big.size() - 1], 0.0f);
}

TEST_CASE("DepthBufferPool: bucket rounding reuses storage across size deltas") {
    chronon3d::DepthBufferPool pool;
    // 641×481 rounds UP to the 704×512 bucket.  A few-pixel delta that stays
    // inside that bucket (e.g. animated transforms varying output size)
    // must reuse the SAME storage — the whole point of the pool.
    auto a = pool.acquire(641, 481);
    auto b = pool.acquire(650, 490);
    CHECK_EQ(a.data(), b.data());
    CHECK_EQ(b.size(), static_cast<size_t>(650) * 490);
    // And a delta that crosses the bucket boundary allocates fresh storage
    // but stays correct + zeroed.
    auto c = pool.acquire(800, 600);
    CHECK_NE(a.data(), c.data());
    CHECK_EQ(c.size(), static_cast<size_t>(800) * 600);
    for (size_t i = 0; i < c.size(); ++i) {
        CHECK_EQ(c[i], 0.0f);
    }
}

TEST_CASE("DepthBufferPool: reset releases storage") {
    chronon3d::DepthBufferPool pool;
    (void)pool.acquire(1920, 1080);
    pool.reset();
    // After reset, a fresh acquire may allocate new storage; must still be
    // correct and zeroed.
    auto span = pool.acquire(320, 240);
    CHECK_EQ(span.size(), static_cast<size_t>(320) * 240);
    for (size_t i = 0; i < span.size(); ++i) {
        CHECK_EQ(span[i], 0.0f);
    }
}

TEST_CASE("DepthBufferPool: grow path re-zeroes used portion (stale-data guard)") {
    chronon3d::DepthBufferPool pool;
    // Simulate a real render: acquire, WRITE depth values, release.
    auto first = pool.acquire(100, 100);
    for (auto& v : first) v = 42.0f;  // stale depth data from "previous frame"
    // Next frame renders at a larger size that crosses the bucket boundary:
    // the grow path must still zero the ENTIRE used portion (resize() only
    // zeroes the newly-appended tail — this guards against the regression
    // where old elements kept stale depth values).
    auto second = pool.acquire(200, 200);
    CHECK_EQ(second.size(), static_cast<size_t>(200) * 200);
    for (size_t i = 0; i < second.size(); ++i) {
        CHECK_EQ(second[i], 0.0f);
    }
}

TEST_CASE("DepthBufferPool: move transfers storage") {
    chronon3d::DepthBufferPool pool;
    auto data_ptr = pool.acquire(100, 100).data();

    chronon3d::DepthBufferPool moved(std::move(pool));
    auto after = moved.acquire(100, 100);
    // Move preserves the buffer (defaulted move semantics the session relies on).
    CHECK_EQ(after.data(), data_ptr);

    // Moved-from pool is still usable (acquire re-allocates).
    auto again = pool.acquire(16, 16);
    CHECK_EQ(again.size(), static_cast<size_t>(16) * 16);
}
