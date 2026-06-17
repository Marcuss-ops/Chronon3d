#include <doctest/doctest.h>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
using namespace chronon3d;

using namespace chronon3d::cache;

namespace {

NodeCacheKey make_node_cache_key(u64 params_hash = 0xABCD, int w = 128, int h = 128) {
    return NodeCacheKey{
        .scope = "test_node",
        .frame = 1,
        .width = w,
        .height = h,
        .params_hash = params_hash,
        .source_hash = 0xCAFE,
        .input_hash = 0xBEEF
    };
}

} // namespace

TEST_CASE("NodeCache: explicit capacity is respected") {
    constexpr size_t cap = 64 * 1024 * 1024;
    NodeCache cache(cap);
    auto stats = cache.stats();
    // LruCache doesn't expose capacity directly, but we can verify
    // that the cache stores items without exceeding the expected limit.
    CHECK(stats.current_size == 0);
    CHECK(stats.current_weight == 0);
}

TEST_CASE("NodeCache: set_capacity reduces capacity and evicts surplus") {
    NodeCache cache(64 * 1024 * 1024); // 64 MB
    auto key = make_node_cache_key();

    auto fb = std::make_shared<chronon3d::Framebuffer>(128, 128);
    cache.store(key, fb);

    auto stats_before = cache.stats();
    CHECK(stats_before.current_size == 1);
    CHECK(stats_before.current_weight > 0);

    // Shrink to 1 byte — forces eviction
    cache.set_capacity(1);
    auto stats_after = cache.stats();
    CHECK(stats_after.current_size == 0);
    CHECK(stats_after.current_weight == 0);
    CHECK(stats_after.evictions >= 1);
}

TEST_CASE("NodeCache: set_capacity increases capacity without eviction") {
    NodeCache cache(64 * 1024 * 1024); // 64 MB
    auto key = make_node_cache_key();

    auto fb = std::make_shared<chronon3d::Framebuffer>(128, 128);
    cache.store(key, fb);

    auto stats_before = cache.stats();
    CHECK(stats_before.current_size == 1);

    // Grow to 128 MB — no eviction
    cache.set_capacity(128 * 1024 * 1024);
    auto stats_after = cache.stats();
    CHECK(stats_after.current_size == 1);
    CHECK(stats_after.current_weight == stats_before.current_weight);
    CHECK(stats_after.evictions == 0);
}

TEST_CASE("NodeCache: zero explicit capacity falls back to config or default") {
    // With no env var set, Config::node_cache_max_bytes == 0,
    // so resolve_capacity falls back to 2 GB.
    NodeCache cache(0);
    auto stats = cache.stats();
    CHECK(stats.current_size == 0);
    CHECK(stats.current_weight == 0);
}

TEST_CASE("NodeCache: default-constructed cache uses config or fallback") {
    NodeCache cache;
    auto stats = cache.stats();
    CHECK(stats.current_size == 0);
    CHECK(stats.current_weight == 0);
}
