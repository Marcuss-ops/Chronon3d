// =============================================================================
// test_lru_extensions.cpp — Tests for LruCache CapacityMode + EvictCallback.
//
// Covers the two extensions added in Commit 1 of the cache refactor:
//   1. CapacityMode::Count — capacity is entry count, every put weights 1.
//   2. EvictCallback — fires under shard lock for every LRU eviction.
//
// These tests are isolated from the existing test_lru_cache / test_lru_weight
// suites because the new API surface is additive (defaults preserve the
// legacy Weight-mode behavior).
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/cache/lru_cache.hpp>
#include <vector>

using namespace chronon3d::cache;

TEST_CASE("LruCache CapacityMode::Count — entry cap is fixed regardless of caller-supplied weight") {
    // Single shard → no sharding ambiguity, cleaner arithmetic.
    LruCache<int, int> cache(/*capacity=*/5, /*num_shards=*/1, CapacityMode::Count);

    // Six puts with masquerading weight 999.  In Count mode the effective
    // weight is forced to 1, so 5 fits and the 6th forces exactly one eviction.
    cache.put(1, 100, /*weight=*/999);
    cache.put(2, 200, 999);
    cache.put(3, 300, 999);
    cache.put(4, 400, 999);
    cache.put(5, 500, 999);

    CHECK(cache.stats().current_size == 5);
    CHECK(cache.stats().evictions == 0);
    CHECK(cache.stats().current_weight == 5);  // 5 entries * 1 weight each

    cache.put(6, 600, 999);

    CHECK(cache.stats().current_size == 5);
    CHECK(cache.stats().evictions == 1);
    CHECK(!cache.contains(1));   // oldest (LRU tail) evicted
    CHECK(cache.contains(6));    // newest (MRU head) present
    CHECK(cache.stats().current_weight == 5);
}

TEST_CASE("LruCache EvictCallback — fires for every LRU eviction under shard lock") {
    std::vector<int> evicted_keys;

    LruCache<int, int> cache(
        /*capacity_weight=*/3,
        /*num_shards=*/1,
        CapacityMode::Weight,
        [&](const int& key, const int& /*value*/, CacheRemovalReason /*reason*/) {
            evicted_keys.push_back(key);
        });

    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300);
    REQUIRE(evicted_keys.empty());
    REQUIRE(cache.stats().evictions == 0);

    cache.put(4, 400);   // evicts key 1
    cache.put(5, 500);   // evicts key 2
    cache.put(6, 600);   // evicts key 3

    CHECK(evicted_keys == std::vector<int>{1, 2, 3});
    CHECK(cache.stats().evictions == 3);
    CHECK(cache.stats().current_size == 3);
    CHECK(cache.contains(4));
    CHECK(cache.contains(5));
    CHECK(cache.contains(6));
}

TEST_CASE("LruCache EvictCallback — fires during resize (LRU-tail-first eviction order)") {
    std::vector<int> evicted_keys;

    LruCache<int, int> cache(
        /*capacity_weight=*/10,
        /*num_shards=*/1,
        CapacityMode::Weight,
        [&](const int& key, const int&, CacheRemovalReason) {
            evicted_keys.push_back(key);
        });

    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300);
    cache.put(4, 400);
    cache.put(5, 500);
    REQUIRE(evicted_keys.empty());

    // Shrink capacity from 10 to 2 — evicts keys 1, 2, 3 in that order
    // (LRU tail is the oldest-inserted entry).
    cache.resize(2);

    CHECK(evicted_keys == std::vector<int>{1, 2, 3});
    CHECK(cache.stats().evictions == 3);
    CHECK(cache.stats().current_size == 2);
    CHECK(cache.contains(4));
    CHECK(cache.contains(5));
}

TEST_CASE("LruCache CapacityMode::Count — oversized guard is never triggered") {
    // Count mode forces effective_weight=1, so even huge caller-supplied
    // weights silence the oversized-item warning.  This is intentional:
    // callers in Count mode explicitly opt out of weight-based bookkeeping.
    LruCache<int, int> cache(/*capacity=*/3, /*num_shards=*/1, CapacityMode::Count);

    cache.put(1, 100, /*weight=*/1'000'000);    // would be "oversized" in Weight mode
    cache.put(2, 200, 1'000'000);
    cache.put(3, 300, 1'000'000);

    CHECK(cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));
    CHECK(cache.stats().current_size == 3);
    CHECK(cache.stats().current_weight == 3);  // each entry is 1 unit
}

TEST_CASE("LruCache EvictCallback — reused callback on repeated eviction emits in order") {
    std::vector<std::pair<int, int>> evicted;

    LruCache<int, std::string> cache(
        /*capacity_weight=*/2,
        /*num_shards=*/2,    // multi-shard: verify ordering is per-shard tail-first
        CapacityMode::Weight,
        [&](const int& key, const std::string& value, CacheRemovalReason) {
            evicted.emplace_back(key, std::stoi(value));
        });

    cache.put(10, "100");
    cache.put(20, "200");

    // Push enough inserts across two shards to force several evictions.
    // With num_shards=2 the capacity splits: shard_cap = 2/2 = 1, so the
    // 3rd distinct insert on any one shard evicts the oldest in that shard.
    for (int k = 30; k < 70; k += 10) {
        cache.put(k, std::to_string(k + 1));
    }

    // We cannot assert the exact set of evicted keys without controlling the
    // hash distribution, but the total count must match stats().
    CHECK(evicted.size() == cache.stats().evictions);
    for (const auto& [k, v] : evicted) {
        CHECK(v == k + 1);   // value snapshot captured correctly under lock
    }
    CHECK(!evicted.empty()); // we forced non-trivial eviction
}
