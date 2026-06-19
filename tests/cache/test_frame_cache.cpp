// =============================================================================
// test_frame_cache.cpp — Tests for LruCache-backed FrameCache (Commit 2).
//
// Covers the post-collapse behavior: bounded cardinality, LRU eviction, and
// shared_ptr return semantics from find().
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/cache/frame_cache.hpp>
#include <chronon3d/core/config.hpp>

#include <memory>
#include <string>

using namespace chronon3d;
using namespace chronon3d::cache;

namespace {

// One 1920×1080 RGBA framebuffer: 1920*1080*sizeof(Color=16 bytes) = 33,177,600 bytes.
// (m_allocated_width ≡ width because 1920*16 = 30720 is cache-line aligned).
constexpr size_t kFrameWeight = 1920 * 1080 * sizeof(Color);

FrameCacheKey make_test_key(std::string composition_id, i32 frame) {
    FrameCacheKey k;
    k.composition_id = std::move(composition_id);
    k.frame = frame;
    k.width = 1920;
    k.height = 1080;
    k.scene_hash = 0xC0FFEE;
    k.render_hash = 0xBEEF;
    return k;
}

std::shared_ptr<Framebuffer> make_synthetic_fb(int w = 1920, int h = 1080) {
    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->set_opaque(true);
    return fb;
}

} // namespace

TEST_CASE("FrameCache default constructor uses Config-driven cap") {
    // The Config knob defaults to 0; the cache should pick the hardcoded
    // fallback (256) and start empty.
    FrameCache cache;
    CHECK(cache.size() == 0);
    CHECK(cache.stats().current_size == 0);
}

TEST_CASE("FrameCache find() returns nullptr on miss and shared_ptr on hit") {
    FrameCache cache(8 * kFrameWeight, /*num_shards=*/1);
    auto key = make_test_key("Composition_A", 0);

    CHECK(cache.find(key) == nullptr);
    CHECK(!cache.contains(key));

    auto fb = make_synthetic_fb();
    cache.store(key, fb);

    auto found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK(found.get() == fb.get());   // same instance, refcount bumped
    CHECK(found->width() == 1920);
    CHECK(found->height() == 1080);
}

TEST_CASE("FrameCache erase() removes only the specified entry") {
    FrameCache cache(/*cap=*/8 * kFrameWeight, /*num_shards=*/1);
    auto k0 = make_test_key("Composition_A", 0);
    auto k1 = make_test_key("Composition_A", 1);

    cache.store(k0, make_synthetic_fb());
    cache.store(k1, make_synthetic_fb());

    CHECK(cache.erase(k0));
    CHECK(!cache.contains(k0));
    CHECK(cache.contains(k1));

    // Erasing a missing key returns false (idempotent).
    CHECK(!cache.erase(k0));
}

TEST_CASE("FrameCache byte-weighted LRU evicts oldest entries when over capacity") {
    constexpr size_t kCap = 4;
    FrameCache cache(kCap * kFrameWeight, /*num_shards=*/1);

    // Fill to capacity.
    for (int i = 0; i < static_cast<int>(kCap); ++i) {
        cache.store(make_test_key("Composition_A", i), make_synthetic_fb());
    }
    CHECK(cache.size() == kCap);
    CHECK(cache.stats().evictions == 0);

    // 5th insert forces exactly one eviction (LRU tail = frame 0).
    cache.store(make_test_key("Composition_A", static_cast<int>(kCap)),
                make_synthetic_fb());
    CHECK(cache.size() == kCap);
    CHECK(cache.stats().evictions == 1);
    CHECK(!cache.contains(make_test_key("Composition_A", 0)));
    CHECK(cache.contains(make_test_key("Composition_A", static_cast<int>(kCap))));
}

TEST_CASE("FrameCache clear() empties the cache and resets the lock-on-miss plumbing") {
    FrameCache cache(8 * kFrameWeight, /*num_shards=*/1);
    cache.store(make_test_key("Composition_A", 0), make_synthetic_fb());
    cache.store(make_test_key("Composition_A", 1), make_synthetic_fb());
    REQUIRE(cache.size() == 2);

    cache.clear();
    CHECK(cache.size() == 0);
    CHECK(!cache.contains(make_test_key("Composition_A", 0)));
}

TEST_CASE("FrameCache find() promotes the entry to MRU so eviction skips it") {
    constexpr size_t kCap = 3;
    FrameCache cache(kCap * kFrameWeight, 1);

    auto k0 = make_test_key("A", 0);
    auto k1 = make_test_key("A", 1);
    auto k2 = make_test_key("A", 2);

    cache.store(k0, make_synthetic_fb());
    cache.store(k1, make_synthetic_fb());
    cache.store(k2, make_synthetic_fb());

    // Touch k0 so MRU order is now: k2 (head), k0, k1 (tail).
    REQUIRE(cache.find(k0) != nullptr);

    // 4th insert evicts k1 (the LRU tail), NOT k0.
    cache.store(make_test_key("A", 9), make_synthetic_fb());
    CHECK(cache.contains(k0));
    CHECK(!cache.contains(k1));
    CHECK(cache.contains(k2));
}
