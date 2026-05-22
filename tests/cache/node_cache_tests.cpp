#include <doctest/doctest.h>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/cache/frame_cache.hpp>
#include <chronon3d/math/color.hpp>
#include <memory>

using namespace chronon3d;
using namespace chronon3d::cache;

TEST_CASE("NodeCacheKey digest changes when fields change") {
    NodeCacheKey a{
        .scope = "layer.effect",
        .frame = 10,
        .width = 1920,
        .height = 1080,
        .params_hash = 1,
        .source_hash = 2,
        .input_hash = 3,
    };
    NodeCacheKey b = a;
    b.frame = 11;

    CHECK(a.digest() != b.digest());
}

TEST_CASE("Test 5.1 — NodeCache cache hit") {
    NodeCache cache;
    NodeCacheKey key{
        .scope = "test",
        .frame = 1,
        .width = 10,
        .height = 10
    };

    auto fb = std::make_shared<Framebuffer>(10, 10);
    cache.store(key, fb);

    auto result = cache.get(key);
    REQUIRE(result != nullptr);
    CHECK(result == fb);
    CHECK(cache.stats().hits == 1);
}

TEST_CASE("Test 5.2 — NodeCache cache miss") {
    NodeCache cache;
    NodeCacheKey key{
        .scope = "unseen",
        .frame = 42
    };

    auto result = cache.get(key);
    CHECK(result == nullptr);
    CHECK(cache.stats().misses == 1);
}

TEST_CASE("Test 5.3 — NodeCache clear svuota cache") {
    NodeCache cache;
    NodeCacheKey key{
        .scope = "test",
        .frame = 2
    };

    auto fb = std::make_shared<Framebuffer>(10, 10);
    cache.store(key, fb);
    CHECK(cache.size() == 1);

    cache.clear();
    CHECK(cache.size() == 0);
    CHECK(cache.get(key) == nullptr);
}

TEST_CASE("Test 5.4 — NodeCache eviction con capacita piccola") {
    // 1 pixel = 16 bytes (RGBA f32). A 10x10 framebuffer size is about 1600 bytes.
    // Set capacity extremely small (e.g., 2000 bytes) so that storing two framebuffers triggers eviction.
    NodeCache cache(2000); 

    NodeCacheKey key1{.scope = "one", .frame = 1, .width = 10, .height = 10};
    NodeCacheKey key2{.scope = "two", .frame = 2, .width = 10, .height = 10};

    auto fb1 = std::make_shared<Framebuffer>(10, 10);
    auto fb2 = std::make_shared<Framebuffer>(10, 10);

    cache.store(key1, fb1);
    CHECK(cache.size() == 1);
    CHECK(cache.stats().evictions == 0);

    cache.store(key2, fb2);
    // Storing second one must evict the first one because 1600 + 1600 = 3200 > 2000
    CHECK(cache.stats().evictions > 0);
    CHECK(cache.size() <= 1);
}

TEST_CASE("NodeCache: replacing existing key does not leave stale LRU entries") {
    NodeCache c(1000);

    auto a = std::make_shared<Framebuffer>(2, 2);
    auto b = std::make_shared<Framebuffer>(2, 2);

    NodeCacheKey key{.scope = "test", .frame = 1};

    c.store(key, a);
    c.store(key, b);

    CHECK(c.size() == 1);
    CHECK(c.contains(key));
    CHECK(c.stats().current_weight == b->size_bytes());
}

TEST_CASE("NodeCache: does not store entries larger than capacity") {
    NodeCache c(10);

    auto fb = std::make_shared<Framebuffer>(2, 2);
    NodeCacheKey key{.scope = "test", .frame = 1};
    c.store(key, fb);

    CHECK_FALSE(c.contains(key));
    CHECK(c.stats().current_weight == 0);
}

TEST_CASE("Test 5.5 — NodeCache key con input_hash diverso non collide") {
    NodeCacheKey key1{
        .scope = "composite",
        .params_hash = 1234,
        .input_hash = 1111
    };

    NodeCacheKey key2{
        .scope = "composite",
        .params_hash = 1234,
        .input_hash = 2222
    };

    CHECK(key1.digest() != key2.digest());
}

TEST_CASE("FrameCache stores and retrieves framebuffer values") {
    FrameCache cache;
    FrameCacheKey key{
        .composition_id = "intro",
        .frame = 42,
        .width = 1280,
        .height = 720,
        .scene_hash = 100,
        .render_hash = 200,
    };

    auto fb = std::make_shared<Framebuffer>(4, 4);
    cache.store(key, fb);

    REQUIRE(cache.contains(key));
    const auto* found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK(found->get() == fb.get());
    cache.clear();
    CHECK(cache.size() == 0);
}
