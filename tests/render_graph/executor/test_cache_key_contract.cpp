#include <doctest/doctest.h>
#include <chronon3d/cache/node_cache.hpp>
#include <unordered_set>
using namespace chronon3d;

using namespace chronon3d::cache;

// ── Cache Key Contract ──────────────────────────────────────────────────────
//
// The cache key MUST include:
//   - scope (node name / identity)
//   - frame
//   - width, height (resolution dependency)
//   - params_hash (node-specific parameters)
//   - source_hash (source content fingerprint)
//   - input_hash (upstream output digest)
//   - tile_x, tile_y, tile_size, tile_hash (tile-based caching)
//
// It MUST NOT silently collide across different node configurations.

TEST_CASE("CacheKeyContract: digest differentiates by scope") {
    NodeCacheKey a{.scope = "source:bg", .frame = 1, .width = 100, .height = 100};
    NodeCacheKey b{.scope = "source:fg", .frame = 1, .width = 100, .height = 100};
    CHECK_NE(a.digest(), b.digest());
}

TEST_CASE("CacheKeyContract: digest differentiates by frame") {
    NodeCacheKey a{.scope = "test", .frame = 0, .width = 100, .height = 100};
    NodeCacheKey b{.scope = "test", .frame = 1, .width = 100, .height = 100};
    CHECK_NE(a.digest(), b.digest());
}

TEST_CASE("CacheKeyContract: digest differentiates by resolution") {
    NodeCacheKey a{.scope = "test", .frame = 0, .width = 1920, .height = 1080};
    NodeCacheKey b{.scope = "test", .frame = 0, .width = 1280, .height = 720};
    CHECK_NE(a.digest(), b.digest());
}

TEST_CASE("CacheKeyContract: digest differentiates by params_hash") {
    NodeCacheKey a{.scope = "test", .frame = 0, .width = 100, .height = 100, .params_hash = 0xABC};
    NodeCacheKey b{.scope = "test", .frame = 0, .width = 100, .height = 100, .params_hash = 0xDEF};
    CHECK_NE(a.digest(), b.digest());
}

TEST_CASE("CacheKeyContract: digest differentiates by source_hash") {
    NodeCacheKey a{.scope = "test", .frame = 0, .width = 100, .height = 100, .source_hash = 1};
    NodeCacheKey b{.scope = "test", .frame = 0, .width = 100, .height = 100, .source_hash = 2};
    CHECK_NE(a.digest(), b.digest());
}

TEST_CASE("CacheKeyContract: digest differentiates by input_hash") {
    NodeCacheKey a{.scope = "test", .frame = 0, .width = 100, .height = 100, .input_hash = 0xAAA};
    NodeCacheKey b{.scope = "test", .frame = 0, .width = 100, .height = 100, .input_hash = 0xBBB};
    CHECK_NE(a.digest(), b.digest());
}

TEST_CASE("CacheKeyContract: digest differentiates by tile position") {
    NodeCacheKey a{.scope = "test", .frame = 0, .width = 100, .height = 100, .tile_x = 0, .tile_y = 0, .tile_size = 64};
    NodeCacheKey b{.scope = "test", .frame = 0, .width = 100, .height = 100, .tile_x = 1, .tile_y = 0, .tile_size = 64};
    CHECK_NE(a.digest(), b.digest());
}

TEST_CASE("CacheKeyContract: digest differentiates by tile_size") {
    NodeCacheKey a{.scope = "test", .frame = 0, .width = 100, .height = 100, .tile_x = 0, .tile_y = 0, .tile_size = 32};
    NodeCacheKey b{.scope = "test", .frame = 0, .width = 100, .height = 100, .tile_x = 0, .tile_y = 0, .tile_size = 64};
    CHECK_NE(a.digest(), b.digest());
}

TEST_CASE("CacheKeyContract: default tile values (-1,-1) produce same digest as pre-tile keys") {
    NodeCacheKey a{.scope = "test", .frame = 0, .width = 100, .height = 100};
    NodeCacheKey b{.scope = "test", .frame = 0, .width = 100, .height = 100, .tile_x = -1, .tile_y = -1};
    CHECK_EQ(a.digest(), b.digest());
}

TEST_CASE("CacheKeyContract: same key is equal to itself and has same digest") {
    NodeCacheKey a{.scope = "glow_node", .frame = 42, .width = 640, .height = 480,
                   .params_hash = 0x12345678, .source_hash = 0xDEADBEEF, .input_hash = 0xCAFEBABE};
    NodeCacheKey b = a;
    CHECK(a == b);
    CHECK_EQ(a.digest(), b.digest());
}

TEST_CASE("CacheKeyContract: NodeCacheKeyHash works in unordered_set") {
    std::unordered_set<NodeCacheKey, NodeCacheKeyHash> set;
    set.insert({.scope = "a", .frame = 0, .width = 100, .height = 100});
    set.insert({.scope = "a", .frame = 1, .width = 100, .height = 100}); // different frame
    set.insert({.scope = "b", .frame = 0, .width = 100, .height = 100}); // different scope

    CHECK(set.size() == 3);

    // Inserting the same key again should not increase size
    set.insert({.scope = "a", .frame = 0, .width = 100, .height = 100});
    CHECK(set.size() == 3);
}
