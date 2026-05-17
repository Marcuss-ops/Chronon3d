#include <doctest/doctest.h>
#include <chronon3d/cache/node_cache.hpp>
#include <memory>

using namespace chronon3d;
using namespace chronon3d::cache;

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

    auto result = cache.find(key);
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

    auto result = cache.find(key);
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
    CHECK(cache.find(key) == nullptr);
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
